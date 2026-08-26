# Свои плагины fluent-bit

Полноценные драйверы PostgreSQL и ClickHouse: каждая СУБД есть и как вход, и
как выход, и на обоих концах данные описываются произвольным SQL. Плюс насос
для переливки между ними. В самих серверах ничего не заводится — ни таблиц, ни
представлений, ни расписаний: работает fluent-bit — данные идут, не работает —
не идут.

| плагин | что делает | как ходит |
|---|---|---|
| `in_postgres` | произвольный SQL → записи или метрики в конвейер | libpq: всё, что она умеет, включая GSSAPI/Kerberos |
| `in_clickhouse` | произвольный SQL → записи или метрики в конвейер | HTTP/HTTPS, `FORMAT JSONEachRow`, TLS |
| `out_postgres` | пачка из конвейера → произвольный SQL над ней | libpq, батч приезжает параметром `$1` типа jsonb |
| `out_clickhouse` | пачка из конвейера → произвольный SQL над ней | HTTP/HTTPS, батч приезжает в `input('схема')` |
| `in_pg2ch` | переливка PostgreSQL → ClickHouse | PG `COPY TO STDOUT` → CH `INSERT FORMAT TabSeparated` |
| `out_ring` | кольцевой буфер, забирают по запросу | свой HTTP: `GET` отдаёт, старое вытесняется по кругу |

Опросные плагины ходят сразу по списку серверов, помнят курсор по каждому и
умеют инкрементальное чтение журналов. Выходные принимают и записи, и метрики.

Отсюда собираются нативные цепочки в любую сторону — например логи nginx с
агрегацией на вставке в ClickHouse, метрики PostgreSQL в ClickHouse или журнал
медленных запросов на отдельный сервер PostgreSQL. Готовый набор —
`examples/example.conf`.

Имя `pgsql` осталось за апстримовским `out_pgsql` (ради него libpq и лежит в
бинарнике), поэтому свои плагины называются `postgres` — симметрично
`clickhouse`.

## Устройство каталога

Каталог на плагин, внутри — файлы `.c`; `include/` — общие заголовки, из него
`.so` не собирается. Сборка идёт в том же образе, где собран сам fluent-bit, и
теми же флагами: они берутся у cmake из `flags.make` родного плагина, поэтому
не разъезжаются при смене версии. Ничего добавлять в Makefile не нужно — новый
каталог подхватывается сам.

Результат — `/usr/local/lib/fluent-bit/plugins/flb-<каталог>.so`, перечисленный
в `/usr/local/etc/fluent-bit/plugins.conf`. Конфигурации достаточно строки

```ini
[SERVICE]
    Plugins_File /usr/local/etc/fluent-bit/plugins.conf
```

Почему `.so`, а не в бинарник: апстрим собирает встроенные плагины только
статически (`add_library(... STATIC ...)`), а вот загружать внешние умеет —
`flb_plugin_load()` делает `dlopen` и ищет структуру `<имя>_plugin`. Патчить
дерево fluent-bit не приходится.

В образе едет и всё остальное, что нужно для работы:

```
/usr/local/share/fluent-bit/queries/pg/*.sql    22 запроса к PostgreSQL
/usr/local/share/fluent-bit/queries/ch/*.sql    12 запросов к ClickHouse
/usr/local/share/fluent-bit/examples/*.conf     готовые конфигурации
```

## Два режима

`Mode` выбирает, что плагин кладёт в конвейер.

| Mode | что получается | кому отдавать |
|---|---|---|
| `logs` (по умолчанию) | запись на строку результата | es, kafka, s3, loki, stdout, pgsql |
| `metrics` | gauge на числовой столбец | prometheus_exporter, prometheus_remote_write, opentelemetry, influxdb |
| `both` | и то, и другое | нужен `Metrics_Tag`, см. ниже |

В режиме `metrics` имя метрики складывается как `<Metric_Prefix>_<столбец>`,
метки — это `instance` плюс перечисленные в `Label_Fields` столбцы:

```ini
[INPUT]
    Name          postgres
    Tag           pg.database
    Mode          metrics
    Metric_Prefix pg_database
    Label_Fields  database
    Targets       ${PG_TARGETS}
    Query_File    /usr/local/share/fluent-bit/queries/pg/database.sql
    Interval_Sec  15

[OUTPUT]
    Name          prometheus_exporter
    Match         pg.*
    Port          2021
```

```
pg_database_xact_commit{instance="pg-1",database="boba"} 4211
pg_database_blks_hit{instance="pg-1",database="boba"} 1837442
```

Метриками становятся все числовые столбцы, кроме меток, `Time_Field` и
`Instance_Field`. `Value_Fields` задаёт список явно — так в метрики не уедет
случайное число вроде `pid` или `oid`. Строки, NULL и json в метрики не
попадают: их место в записи либо в метке.

Отдавать надо **счётчик, а не скорость**: разность считает получатель
(`rate(pg_database_xact_commit[1m])`), и делает это правильнее, чем клиент,
который не знает про перезапуски и пропуски опроса.

В режиме `both` один выход не может принять и записи, и метрики —
`prometheus_exporter` разберёт только метрики. Разводит их `Metrics_Tag`:
записи уходят под `Tag`, метрики под `Metrics_Tag`.

Почему не штатный `filter log_to_metrics`: он делает одну метрику на блок
`[FILTER]`. Для запроса с двадцатью счётчиками это двадцать блоков, и метки
берутся только из полей записи. Пример на фильтрах есть —
в `examples/example.conf`, — он для случая, когда метрику надо собрать из
уже готового потока записей.

## Метки: чего не делать

Метка с неограниченным числом значений порождает новый временной ряд на каждое
значение и разносит хранилище. Не метка: `pid`, имя куска ClickHouse, `query_id`,
текст запроса, имя файла WAL. Метка: база, схема, таблица, тип ожидания, движок.

Поэтому запросы для метрик свёрнуты на стороне сервера (`GROUP BY`), а
подробности отданы отдельными запросами для режима `logs`: `parts.sql` против
`parts-detail.sql`, `waits.sql` против `activity.sql`.

## Инкрементальное чтение журналов

В запросе пишется `{CURSOR}`, плагин подставляет вместо него последнее
увиденное значение `Cursor_Field` — по серверу своё, поэтому отставшая нода не
теряет своих событий. `Cursor_File` — путь-основа, к нему дописывается
`.<host>-<port>`: у каждого сервера свой файл, перезапуск не приносит уже
прочитанное заново.

```sql
SELECT hostName() AS instance,
       toString(event_time_microseconds) AS event_ts, ...
FROM system.part_log
WHERE event_time_microseconds > {CURSOR}
ORDER BY event_time_microseconds
FORMAT JSONEachRow
```

```ini
    Cursor_Field  event_ts
    Cursor_File   /var/lib/fluent-bit/ch-part_log
    Time_Field    event_ts
```

Тонкости:

- Столбец под своим именем в `SELECT` переименовывать обязательно: алиас
  `event_time_microseconds` подменил бы столбец в `WHERE`, и сравнение
  `DateTime64` со `String` упало бы.
- `Cursor_Type` говорит, как подставлять значение: `datetime64` (по умолчанию у
  ClickHouse, оборачивает в `toDateTime64(…, 6)`), `string` (по умолчанию у
  PostgreSQL, закавычивает), `number` (без кавычек — для курсора по id) и `raw`.
  Для курсора по `id` в ClickHouse менять умолчание обязательно:
  `toDateTime64('42', 6)` — это `CANNOT_PARSE_DATETIME`, а не число.
- `Cursor_Default` живёт по другим правилам: он подставляется дословно, как
  фрагмент SQL, и `Cursor_Type` на него не действует. Умолчание
  `now() - INTERVAL …` осмысленно только для курсора по времени; по `id` там
  должен стоять `0`.
- Отметка хранится строкой (127 байт) и максимум по пачке выбирается `strcmp`,
  то есть лексически: из `1…12` наибольшим окажется `9`. По числовому столбцу
  это даёт повторное чтение хвоста (`at-least-once`, курсор догоняет на
  следующем тике), а вот по текстовому столбцу с числами строки теряются
  по-настоящему — там лексический порядок уже у самой базы. `in_clickhouse`
  вдобавок берёт из ответа только строку или неотрицательное целое — по
  `Float64` курсор не сдвинется. Разбор с захватами — в `examples/README.md`,
  «Как рендерится `{CURSOR}`».
- `Time_Field` ставит записи время события, а не время опроса. Без него порядок
  у получателя разъедется с порядком в журнале. Понимает
  `2026-08-24 12:00:00.123456+03`, ISO-8601 и epoch; без смещения считает UTC.

## in_clickhouse

```ini
[INPUT]
    Name          clickhouse
    Tag           ch.parts
    Targets       clickhouse-01:8123,clickhouse-02:8123
    User          default
    Password      ${CH_PASSWORD}
    Query_File    /usr/local/share/fluent-bit/queries/ch/parts.sql
    Interval_Sec  5
```

| параметр | по умолчанию | что делает |
|---|---|---|
| `Targets` | — | ноды `host:port` через запятую; порт 8123, с `Tls On` — 8443 |
| `Query` / `Query_File` | — | текст запроса или файл с ним; файл важнее |
| `User`, `Password` | — | уезжают в заголовки `X-ClickHouse-User` / `X-ClickHouse-Key` |
| `Database` | — | база по умолчанию для запроса |
| `Instance_Field` | `instance` | поле с именем ноды; если запрос его отдал (`hostName()`), плагин своё не добавляет |
| `Time_Field` | — | поле со временем события |
| `Cursor_Field` | — | поле, из которого берётся курсор |
| `Cursor_File` | — | путь-основа для курсора между перезапусками (на ноду свой файл) |
| `Cursor_Type` | `datetime64` | `datetime64`, `string`, `number`, `raw` |
| `Cursor_Default` | `now() - INTERVAL 30 SECOND` | чем подставить `{CURSOR}` на первом запросе |
| `Mode` | `logs` | `logs`, `metrics`, `both` |
| `Metric_Prefix` | `clickhouse` | начало имени метрик |
| `Metrics_Tag` | — | тег метрик в режиме `both` |
| `Label_Fields` | — | поля-метки через запятую |
| `Value_Fields` | — | поля-значения; пусто — все числовые, кроме меток |
| `Buffer_Max_Size` | `0` (без ограничения) | потолок ответа |
| `Tls`, `Tls.Verify`, `Tls.Ca_File`, … | `Off` | HTTPS-интерфейс. Это свойства **ядра**: плагин берёт у него готовый контекст. Полный список — `examples/README.md` |
| `Interval_Sec` | `5` | период опроса |

Неочевидное:

- `Buffer_Max_Size` стоит оставить нулевым: у HTTP-клиента fluent-bit ответ по
  умолчанию ограничен 4 КБ, и превышение — не обрезка, а ошибка всего запроса.
  Системные таблицы кластера — мегабайты.
- В `JSONEachRow` 64-битные целые приезжают **строками**
  (`output_format_json_quote_64bit_integers` включён по умолчанию). Плагин
  разбирает числовые строки сам, поэтому в метрики они попадают числами.
- `system.part_log` и `system.query_log` создаются лениво — на пустом сервере
  их нет, и запрос вернёт ошибку до первой записи. Это не сбой сбора.

## in_pgsql

```ini
[INPUT]
    Name          postgres
    Tag           pg.database
    Targets       postgres-17:5432,postgres-18:5432
    User          boba-svc
    Database      postgres
    Query_File    /usr/local/share/fluent-bit/queries/pg/database.sql
    Interval_Sec  5
```

| параметр | по умолчанию | что делает |
|---|---|---|
| `Targets` | — | серверы `host[:port]` через запятую, порт 5432 |
| `User`, `Password`, `Database` | — | при Kerberos пароль не нужен, имя роли — как принципал |
| `Conn_Options` | — | довесок к строке подключения libpq, например `sslmode=require` |
| `Query` / `Query_File` | — | текст запроса или файл с ним; файл важнее |
| `Instance_Field` | `instance` | куда положить имя сервера; если такой столбец есть в результате, плагин своё не добавляет; пустое — не добавлять |
| `Time_Field` | — | столбец со временем события |
| `Cursor_Field`, `Cursor_File` | — | как у `in_clickhouse` |
| `Cursor_Type` | `string` | `string`, `number`, `raw` |
| `Cursor_Default` | `now() - INTERVAL '30 seconds'` | чем подставить `{CURSOR}` на первом запросе |
| `Mode` | `logs` | `logs`, `metrics`, `both` |
| `Metric_Prefix` | `pgsql` | начало имени метрик |
| `Metrics_Tag` | — | тег метрик в режиме `both` |
| `Label_Fields`, `Value_Fields` | — | метки и значения |
| `Connect_Timeout` | `5` | секунды |
| `Statement_Timeout` | `5000` | миллисекунды, ставится сеансу |
| `Retry_Pause_Sec` | `30` | пауза после неудачного подключения |
| `Interval_Sec` | `5` | период опроса |

Линковать libpq не нужно: она уже в бинарнике ради `out_pgsql`, а fluent-bit
собран с `ENABLE_EXPORTS`, поэтому символы `PQ*` видны из загруженной `.so`.
Оттуда же берётся всё, что libpq умеет по части аутентификации.

Типы столбцов не теряются: по OID из `PQftype` целые приезжают целыми,
`numeric`/`float` — числами, `bool` — булевым, `json`/`jsonb` — вложенным
объектом, NULL — null. Всё остальное приезжает строкой, как его отдал сервер.

Пустой `Targets` — не ошибка, а выключенный вход: так гасится запрос к
представлению, которого нет ни на одном сервере этой установки.

### Kerberos

Билет берётся из keytab сам, `kinit` не нужен — достаточно окружения:

```
KRB5_CLIENT_KTNAME=/path/client.keytab
KRB5CCNAME=FILE:/var/lib/fluent-bit/krb5cc
KRB5_CONFIG=/path/krb5.conf
```

В `krb5.conf` клиента важно `dns_canonicalize_hostname = false`. Иначе в сети
docker имя `postgres-17` разворачивается в `postgres-17.docker`, клиент просит
билет в realm `DOCKER`, и получает `Server krbtgt/DOCKER@… not found`.
С выключенной канонизацией он просит ровно `postgres/postgres-17@REALM` —
такой SPN в серверном keytab обычно и лежит.

Роль в базе должна называться так же, как принципал (при `include_realm=0` в
`pg_hba.conf` — без realm). Иначе сервер отвечает `role "…" does not exist`
уже после успешного рукопожатия.

## in_pg2ch: переливка PostgreSQL → ClickHouse

Насос: PG `COPY (SELECT ...) TO STDOUT` (text-формат) батчами перекладывается
в CH `INSERT ... FORMAT TabSeparated`. Форматы почти байт-совместимы — `\t`,
`\N`, перевод строки и backslash-эскейпы совпадают, — поэтому данные идут без
построчного разбора: ~1 млн строк/с на стенде. В конвейер fluent-bit данные не
попадают, только запись-отчёт о прогоне; вход работает в собственном потоке
(`FLB_INPUT_THREADED`), долгий перегон не останавливает остальные входы.

```ini
[INPUT]
    Name          pg2ch
    Tag           pump.events
    Pg_Target     pg-1:5432
    Pg_Database   src
    Ch_Target     ch-1:8123
    Ch_Database   dst
    Ch_Table      events
    Query         SELECT id, ts AT TIME ZONE 'UTC', kind, payload FROM events WHERE id > {CURSOR} ORDER BY id
    Cursor_Query  SELECT coalesce(max(id), 0) FROM dst.events
    Batch_Bytes   64M
    Interval_Sec  60
```

| параметр | по умолчанию | что делает |
|---|---|---|
| `Pg_Target`, `Pg_User`, `Pg_Password`, `Pg_Database`, `Pg_Options` | — | источник; Kerberos как у `in_postgres` |
| `Ch_Target`, `Ch_User`, `Ch_Password`, `Ch_Database`, `Ch_Table` | — | приёмник |
| `Ch_Columns` | — | столбцы вставки в порядке SELECT, если порядок в таблице другой |
| `Ch_Settings` | `date_time_input_format=best_effort` | настройки CH для вставки, `key=value` через запятую |
| `Query` / `Query_File` | — | SELECT для COPY; `{CURSOR}` заменяется отметкой |
| `Cursor_Query` | — | SQL к CH за отметкой; пусто — полный перегон каждый тик |
| `Cursor_Default` | `0` | отметка при пустой цели |
| `Batch_Bytes` | `64M` | порог отправки батча; каждая вставка — кусок MergeTree |
| `Report_Empty` | `off` | писать отчёт и о пустых прогонах |
| `Statement_Timeout` | `0` (без ограничения) | большой перегон дольше 5 секунд — это норма |
| `Interval_Sec` | `60` | период прогонов |

Как устроен инкремент — **high-water mark в целевой таблице**, курсорных файлов
нет: `Cursor_Query` спрашивает отметку у CH, она подставляется в `{CURSOR}`
запроса к PG. Упавший посреди прогона перегон безопасен — доехавшие батчи
подняли отметку, следующий тик продолжит ровно с места падения (проверено:
ошибка вставки → отметка на месте → после починки те же строки доехали без
дублей). Отсюда два требования к запросу:

- `ORDER BY` по столбцу отметки обязателен;
- строки с одинаковым значением отметки должны помещаться в один прогон —
  для монотонного `id` это само собой.

Кавычки вокруг `{CURSOR}` плагин не добавляет: отметка приезжает готовым
литералом, а числовая в кавычках сравнилась бы строкой. Нужны кавычки — они
пишутся в запросе: `'{CURSOR}'`.

### Отметка не только по числу

Значение из `Cursor_Query` подставляется в `{CURSOR}` **дословно**, поэтому
годится любой тип, который PostgreSQL умеет сравнивать. Кавычки и приведение —
задача автора запроса:

| тип отметки | `Query` | `Cursor_Query` |
|---|---|---|
| `bigint` | `WHERE id > {CURSOR}` | `SELECT coalesce(max(id), 0) FROM dst` |
| `timestamptz` | `WHERE ts > '{CURSOR}'::timestamptz` | `SELECT max(ts) FROM dst` |
| `text` | `WHERE k > '{CURSOR}'` | `SELECT max(k) FROM dst` |
| `uuid` v7 | `WHERE u > '{CURSOR}'::uuid` | `SELECT max(u) FROM dst` |

`coalesce` нужен не всегда: у ненулевого столбца ClickHouse на пустой таблице
возвращает значение по умолчанию — для `DateTime64` это
`1970-01-01 00:00:00.000000`, готовый литерал для первого прогона. Для
`Nullable`-столбца `coalesce` обязателен, иначе в запрос уедет `\N`.

Проверено на стенде: отметка по `timestamptz` перегнала 1000 строк первым
прогоном (отметка `1970-01-01 00:00:00.000000`), потом 300 долитых —
инкрементально, без дублей.

**Но у неуникальной отметки есть ловушка, и она стоит того, чтобы про неё
знать.** Сравнение строгое (`>`), поэтому строка, чья отметка **ровно равна**
текущей, не приедет никогда. Проверено: добавили в источник строку с `ts` в
точности равным отметке в целевой таблице — она не доехала ни за один
последующий прогон. Для `id` это неважно (он уникален), для времени — реальный
риск: события одной микросекунды, пакетная вставка одним `now()`, опоздавшая
запись задним числом.

Лечится двумя способами, и оба со своей ценой:

- `>=` вместо `>` — потерянная строка приезжает, но пограничные строки
  переливаются **каждый тик**. Проверено: дубли росли на 2 за прогон и дальше
  копились. Значит целевая таблица обязана быть `ReplacingMergeTree` с ключом
  по первичному полю, иначе они просто накапливаются;
- отметка по уникальному монотонному столбцу (`id bigserial`, UUID v7) — тогда
  строгое `>` безопасно, дублей нет вовсе. Это и есть путь по умолчанию.

Отдельно про точность: если в PostgreSQL `timestamptz` с микросекундами, а в
ClickHouse столбец `DateTime` (секунды), отметка на приёмнике **округляется
вниз**, и вся последняя секунда будет переливаться заново каждый прогон. Тип на
приёмнике должен быть не грубее источника — `DateTime64(6)`.

Приведения типов — в SELECT-части, это ответственность запроса:

| тип PG | что писать | почему |
|---|---|---|
| `boolean` | `ok::int` | COPY text отдаёт `t`/`f`, CH их не разберёт |
| `timestamptz` | `ts AT TIME ZONE 'UTC'` | суффикс зоны `+03`; цель — `DateTime64(6,'UTC')` |
| `bytea` | `encode(b, 'base64')` или hex | сырые байты в text-формате не переживут |
| массивы | `array_to_string(...)` | `{a,b}` и `['a','b']` не совпадают |

Остальное — целые, `numeric`→`Decimal`, `float`, `date`, текст с любыми
табами/переводами/бэкслешами, юникод, NULL — проходит как есть (проверено на
стенде сверкой байт-в-байт).

Отчёт-запись на прогон: `table`, `status`, `rows`, `bytes`, `batches`,
`duration_ms`, `cursor`, `error`. Метрики из него делает штатный
`filter log_to_metrics` — см. `examples/example.conf`.

Когда насос не нужен: если Kerberos к PG не требуется и нагрузка на ноду CH не
смущает, тот же перегон делается вообще без плагина — `in_clickhouse` по
расписанию гонит `INSERT INTO dst SELECT ... FROM postgresql(...) WHERE id >
(SELECT max(id) FROM dst)`: внутри `postgresql()` CH сам выполняет в PG
`COPY (SELECT ...) TO STDOUT` и проталкивает предикат (проверено по логу PG).
Для CDC (UPDATE/DELETE тоже едут) в сборке CH есть `MaterializedPostgreSQL` —
там fluent-bit не нужен вовсе.

## Выходы: SQL над пачкой

`out_postgres` и `out_clickhouse` берут пачку событий, которую конвейер отдал
на сброс, и выполняют над ней **один** запрос. Значения в текст запроса не
подставляются вовсе — пачка едет отдельно как JSON, разбирает её сам сервер.
Экранировать нечего, инъекции взяться неоткуда.

```ini
# просто положить записи в таблицу: поля → одноимённые столбцы
[OUTPUT]
    Name     clickhouse
    Match    nginx.*
    Host     ch-1
    Database logs
    Table    nginx_raw

# то же, но со свёрткой прямо на вставке
[OUTPUT]
    Name     clickhouse
    Match    nginx.*
    Host     ch-1
    Database logs
    # значение — всегда одна строка: переносов классический формат не понимает,
    # для длинного SQL есть Query_File
    Query    INSERT INTO nginx_minute SELECT toStartOfMinute(timestamp) AS minute, host, status, count() AS hits, sum(bytes) AS bytes FROM input('timestamp DateTime64(6), host String, status UInt16, bytes UInt64') GROUP BY minute, host, status
```

```ini
# PostgreSQL: пачка — это $1, массив объектов jsonb
[OUTPUT]
    Name     postgres
    Match    nginx.*
    Host     pg-1
    Database logs
    Query    INSERT INTO nginx_raw SELECT * FROM jsonb_populate_recordset(null::nginx_raw, $1)

# с разбором схемы и фильтром — всё в том же запросе
[OUTPUT]
    Name     postgres
    Match    pg.slow
    Host     pg-audit
    Database audit
    Query    INSERT INTO slow_queries (ts, queryid, mean_ms) SELECT timestamp, queryid, mean_exec_time FROM jsonb_to_recordset($1) AS x(timestamp timestamptz, queryid text, mean_exec_time double precision) WHERE mean_exec_time > 100
```

| параметр | по умолчанию | что делает |
|---|---|---|
| `Host`, `Port` | `127.0.0.1`, 8123/5432 | адрес сервера |
| `User`, `Password`, `Database` | — | у postgres при Kerberos пароль не нужен |
| `Table` | — | простая вставка: поля записи → одноимённые столбцы |
| `Query` / `Query_File` | — | произвольный SQL; важнее, чем `Table` |
| `Time_Key` | `timestamp` | поле, куда положить время события; пустое — не добавлять |
| `Time_Format` | `datetime64` (ch), `iso8601` (pg) | `datetime64`, `iso8601`, `epoch` |
| `Skip_Unknown_Fields` (ch) | `on` | лишние поля записи не роняют вставку |
| `Settings` (ch) | — | настройки CH, `key=value` через запятую |
| `Conn_Options` (pg) | — | довесок к строке подключения libpq |
| `Statement_Timeout` (pg) | `30000` | миллисекунды |
| `Tls*` (ch) | `Off` | HTTPS-интерфейс; свойства **ядра**, не плагина |

Метрики (`event_type metrics`) выходы тоже принимают: каждая разворачивается в
строку **фиксированной** формы, поэтому схема в запросе описывается один раз и
не зависит от того, какие метрики придут.

```json
{"name":"pg_database_xact_commit","type":"counter",
 "labels":{"instance":"pg-1","database":"boba"},
 "value":4211,"timestamp":"2026-08-24 10:00:00.000000"}
```

```ini
    Query INSERT INTO series SELECT timestamp, name, labels, value FROM input('name String, type String, labels Map(String,String), value Float64, timestamp DateTime64(6)')
```

Гистограммы и сводки пропускаются: у них не одно значение, а набор корзин, и в
плоскую строку они не ложатся.

Про ошибки: 4xx от ClickHouse и ошибки схемы/данных от PostgreSQL считаются
неисправимыми (`FLB_ERROR`) — повтор не поможет, а место в буфере займёт.
Обрывы связи, нехватка ресурсов и 5xx уходят в повтор (`FLB_RETRY`), то есть
подчиняются штатным `Retry_Limit` и буферу fluent-bit.

Важно про семантику: конвейер даёт **at-least-once**. При ретрае после частично
удавшейся вставки строки могут задвоиться, а при исчерпании `Retry_Limit` —
потеряться. Для журналов это нормально (`ReplacingMergeTree` на стороне CH
закрывает дубли), для репликации таблицы — нет: там нужен `in_pg2ch`, у
которого отметка живёт в самой целевой таблице.

## out_ring: кольцевой буфер с выдачей по запросу

Выход, который **никуда не отправляет**. События копятся в кольце на
`Ring_Size` байт; когда набранное превышает лимит, вытесняется самое старое.
Забирает их потребитель сам:

```sh
curl http://collector:2022/logs           # отдать, ничего не трогая
curl http://collector:2022/logs/clear     # отдать и очистить
curl -X DELETE http://collector:2022/logs # то же самое, если удобнее методом
curl http://collector:2022/stats          # сколько лежит и сколько вытеснено
```

```ini
[OUTPUT]
    Name      ring
    Match     log.nginx
    Host      127.0.0.1
    Port      2022
    Uri       /logs
    Ring_Size 64M
```

```json
{"records":98,"bytes":15824,"limit":16000,"dropped":911,"received":1009,"served":392}
```

| параметр | по умолчанию | что делает |
|---|---|---|
| `Host`, `Port` | `0.0.0.0`, `2022` | где слушать (параметры ядра) |
| `Uri` | `/logs` | путь выдачи без очистки |
| `Clear_Uri` | `<Uri>/clear` | путь «отдать и очистить» |
| `Stats_Uri` | `/stats` | счётчики кольца; пустой — не отдавать |
| `Ring_Size` | `64M` | размер кольца в байтах |
| `Time_Key`, `Time_Format` | `timestamp`, `iso8601` | как добавить время события |

Формат выдачи — JSON-строки, по событию на строку; принимаются и записи, и
метрики. Очистка происходит **только если ответ удалось отдать**: не удалось —
записи остаются и уедут следующим запросом.

Одновременные запросы сериализуются: кольцо живёт под мьютексом, и он держится
на всё время «собрать → отдать → очистить». Проверено залпом из восьми
параллельных `curl`: все 74 записи получил первый, остальные семь — пусто,
дублей нет.

Чего у этого выхода нет:

- **гарантий конвейера.** Плагин отвечает «принято» сразу, как положил записи в
  кольцо, поэтому `Retry_Limit` и дисковый буфер их больше не страхуют. Кольцо
  и есть буфер, и оно по определению теряет самое старое;
- **переживания перезапуска.** Кольцо в памяти: рестарт fluent-bit — и оно пусто;
- **TLS и авторизации.** Как и у `prometheus_exporter` — закрывается прокси
  (см. «Защита входящих соединений» в `examples/README.md`).

Про `?clear=1`: параметр поддержан, но работает не везде. В 3.2 и 4.x строка
запроса до плагина доходит, в 5.x — нет: там путь приезжает от monkey уже
обрезанным, а его буфер к моменту вызова обработчика переиспользован (проверено
отладочной печатью — `query_string`, `uri` и `uri_processed` пусты). Поэтому
основной способ — отдельный путь `Clear_Uri` или метод `DELETE`: они работают
одинаково на всех четырёх версиях.

Внутри плагин поднимает собственный HTTP-сервер тем же способом, что и штатный
`prometheus_exporter`. Слой этот менялся: в 5.x — `flb_http_server_*`, в 3.2/4.x
— напрямую встроенный monkey, поэтому в файле две половины под `#if`. Символы
обоих наборов экспортированы из бинарника, так что внешней `.so` они доступны.

## Библиотека запросов

`queries/pg` — 22 запроса: `meta`, `database`, `bgwriter`, `checkpointer`
(и `checkpointer-16` для 14..16), `wal`, `waldir`, `archiver`, `io` (16+),
`waits`, `activity`, `locks`, `tables`, `indexes`, `bloat`, `visibility`,
`buffers`, `replication`, `slots`, `statements`, `settings`, `progress`.

`queries/ch` — 12: `metrics`, `asynchronous_metrics`, `tables`, `parts`,
`parts-detail`, `merges`, `replicas`, `errors`, `columns`, `clusters`,
`part_log`, `query_log`.

В шапке каждого файла сказано, что он показывает и какие столбцы имеет смысл
брать метками, а какие значениями. Часть требует расширений (`pg_stat_statements`,
`pg_buffercache`, `pg_visibility`) или версии сервера — это тоже написано там же.

## Примеры

| файл | о чём |
|---|---|
| `example.conf` | один рабочий пример на все возможности: оба входа, оба выхода, насос, метрики, приём извне |
| `README.md` | как выглядит поток событий на входе и на выходе + разбор каждого параметра |

Запускается как есть:

```sh
docker run --rm -e PG_TARGETS=pg-1:5432 -e PG_USER=postgres -e PG_PASSWORD=… \
    -e PG_DATABASE=postgres -e CH_TARGETS=ch-1:8123 -e CH_USER=default \
    -e CH_PASSWORD=… -p 2021:2021 dmp/fluent-bit:5.0 \
    -c /usr/local/share/fluent-bit/examples/example.conf
curl localhost:2021/metrics
```

## Стенд

`test/stand` поднимает postgres + clickhouse + fluent-bit и проверяет весь
путь: метрики в экспортёре, метки из столбцов, отсев по `Value_Fields`, режим
`both` и курсор после перезапуска.

```sh
docker compose -f test/stand/docker-compose.yml up -d
test/stand/check.sh          # сам наполнит данными и проверит
docker compose -f test/stand/docker-compose.yml down -v
```

Другая версия образа — `FLB_IMAGE=dmp/fluent-bit:3.2.10 docker compose … up -d`.
