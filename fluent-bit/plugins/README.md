# Свои плагины fluent-bit

Каталог на плагин, внутри — файлы `.c`. Сборка происходит в том же образе, где
собран сам fluent-bit, и теми же флагами: они берутся у cmake из `flags.make`
родного плагина, поэтому не разъезжаются при смене версии. Ничего добавлять в
Makefile не нужно — новый каталог подхватывается сам.

Результат — `/usr/local/lib/fluent-bit/plugins/flb-<каталог>.so`, перечисленный в
`/usr/local/etc/fluent-bit/plugins.conf`. Конфигурации достаточно строки

```ini
[SERVICE]
    Plugins_File /usr/local/etc/fluent-bit/plugins.conf
```

Почему `.so`, а не в бинарник: апстрим собирает встроенные плагины только
статически (`add_library(... STATIC ...)`), а вот загружать внешние умеет —
`flb_plugin_load()` делает `dlopen` и ищет структуру `<имя>_plugin`. Патчить
дерево fluent-bit не приходится.

## in_clickhouse

Входной плагин: выполняет SQL в ClickHouse по HTTP и отдаёт строки ответа
(`FORMAT JSONEachRow`) как записи fluent-bit. Один вход опрашивает сразу все
перечисленные ноды.

```ini
[INPUT]
    Name          clickhouse
    Tag           ch.parts
    Targets       clickhouse-01:8123,clickhouse-02:8123
    User          default
    Password      ${CH_PASSWORD}
    Query_File    /opt/queries/parts.sql
    Interval_Sec  5
```

| параметр | по умолчанию | что делает |
|---|---|---|
| `Targets` | — | ноды `host:port` через запятую, порт по умолчанию 8123 |
| `Query` / `Query_File` | — | текст запроса или файл с ним; файл важнее |
| `User`, `Password` | — | уезжают в заголовки `X-ClickHouse-User` / `X-ClickHouse-Key` |
| `Database` | — | база по умолчанию для запроса |
| `Cursor_Field` | — | поле ответа, из которого берётся курсор |
| `Cursor_File` | — | путь-основа, где курсор переживает перезапуск (на ноду свой файл) |
| `Cursor_Default` | `now() - INTERVAL 30 SECOND` | чем подставить `{CURSOR}` на первом запросе |
| `Buffer_Max_Size` | `0` (без ограничения) | потолок ответа |
| `Interval_Sec` | `5` | период опроса |

Инкрементальное чтение журналов: в запросе пишется `{CURSOR}`, а плагин
подставляет вместо него последнее увиденное значение `Cursor_Field` — по ноде
своё, поэтому отставшая нода не теряет свои события.

```sql
SELECT hostName() AS instance, toString(event_time_microseconds) AS event_ts, ...
FROM system.part_log
WHERE event_time_microseconds > {CURSOR}
ORDER BY event_time_microseconds
FORMAT JSONEachRow
```

Курсор берётся строкой как есть и оборачивается в `toDateTime64(..., 6)`.
Столбец под своим именем в `SELECT` переименовывать обязательно: алиас
`event_time_microseconds` подменил бы столбец в `WHERE`, и сравнение
`DateTime64` со `String` упало бы.

`Buffer_Max_Size` стоит оставить нулевым: у HTTP-клиента fluent-bit ответ по
умолчанию ограничен 4 КБ, и превышение — не обрезка, а ошибка всего запроса.
Системные таблицы кластера — мегабайты.

## in_pgsql

То же для PostgreSQL, но через libpq. Линковать её не нужно: она уже в
бинарнике ради `out_pgsql`, а fluent-bit собран с `ENABLE_EXPORTS`, поэтому
символы `PQ*` видны из загруженной `.so`. Оттуда же берётся всё, что libpq
умеет по части аутентификации, — в том числе GSSAPI.

```ini
[INPUT]
    Name          pgsql
    Tag           pg.database
    Targets       postgres-17:5432,postgres-18:5432
    User          boba-svc
    Database      postgres
    Query_File    /opt/queries/database.sql
    Interval_Sec  5
```

| параметр | по умолчанию | что делает |
|---|---|---|
| `Targets` | — | серверы `host[:port]` через запятую, порт по умолчанию 5432 |
| `User`, `Password`, `Database` | — | при Kerberos пароль не нужен, имя роли — как принципал |
| `Conn_Options` | — | довесок к строке подключения libpq, например `sslmode=require` |
| `Query` / `Query_File` | — | текст запроса или файл с ним; файл важнее |
| `Instance_Field` | `instance` | куда положить имя сервера; пустое — не добавлять |
| `Cursor_Field`, `Cursor_File`, `Cursor_Default` | — / — / `now() - INTERVAL '30 seconds'` | как у `in_clickhouse` |
| `Connect_Timeout` | `5` | секунды |
| `Statement_Timeout` | `5000` | миллисекунды, ставится сеансу |
| `Retry_Pause_Sec` | `30` | пауза после неудачного подключения |
| `Interval_Sec` | `5` | период опроса |

Типы столбцов не теряются: по OID из `PQftype` целые приезжают целыми,
`numeric`/`float` — числами, `bool` — булевым, `json`/`jsonb` — вложенным
объектом, NULL — null. Всё остальное приезжает строкой, как его отдал сервер.

Имя сервера добавляет плагин (`Instance_Field`): у PostgreSQL нет своего
`hostName()`, а различать источники на той стороне надо.

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
