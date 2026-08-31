# rsyslog-ext/clickhouse

Сборка выходного модуля **`omclickhouse`** для rsyslog — отправка журнала в
ClickHouse по HTTP. Результат — один файл `omclickhouse.so`, который кладётся
в каталог модулей системного `rsyslogd` (на RED OS 8 — `/usr/lib64/rsyslog/`).

Модуль не отдельный проект: он лежит в дереве исходников самого rsyslog
(`plugins/omclickhouse`), и апстрим держит его выключенным (`--enable-clickhouse`
по умолчанию `no`) — поэтому в пакетах его нет ни в astra, ни в RED OS.

## Команды

| Команда | Что делает |
|---------|------------|
| `make help` | список команд (по умолчанию) |
| `make sources` | скачать исходники (rsyslog, libfastjson, curl) в `$(ARTIFACTS)/src/` |
| `make dist` | собрать `dmp/rsyslog-clickhouse:<версия>-dist` |
| `make extract` | выложить модуль из `-dist` образа в `$(ARTIFACTS)/build/<версия>` |
| `make build` | собрать `dmp/rsyslog-clickhouse:<версия>` |
| `make test` | проверки модуля в образе |
| `make all` | `sources` + `dist` + `build` + `test` |

## Установка на целевую машину

```sh
make all extract
scp ci-artifacts/rsyslog-ext/clickhouse/build/8.2602.0/usr/lib64/rsyslog/omclickhouse.so \
    target:/usr/lib64/rsyslog/
scp rsyslog-ext/clickhouse/conf.d/omclickhouse.conf.example \
    target:/etc/rsyslog.d/omclickhouse.conf
# на целевой машине
rsyslogd -N1 && systemctl restart rsyslog
```

`omclickhouse.so` — 132 КБ, не стриплен: отладочная информация занимает
немного, а падение внутри чужого `rsyslogd` без неё не разобрать.

Версия модуля обязана совпадать с версией `rsyslogd` на машине: модуль грузится
через `dlopen` внутрь процесса и работает со структурами rsyslog напрямую.
`rsyslogd -v` покажет версию, `VERSION_CHAIN` в `Makefile` — под какие собираем.

## Почему сборка на glibc 2.28

Модуль ставится к чужому `rsyslogd`, поэтому связей у него должно быть как
можно меньше, а требования к системе — как можно ниже. Проверяется прямо в
`Dockerfile`, сборка падает при нарушении:

* нет `RPATH`/`RUNPATH` на сборочные каталоги;
* из чужих библиотек только `libcurl.so.4` (плюс `libc`/`libm`/`libpthread`/`libresolv`);
* потолок версий символов — `GLIBC_2.17`, фактически модуль требует `GLIBC_2.14`.

Символы самого rsyslog (`obj`, `glbl`, `LogError`, …) и `libestr`/`libfastjson`
в модуль не линкуются: они находятся в процессе `rsyslogd`, который собран с
`-export-dynamic`. Поэтому версии этих библиотек на целевой машине не важны.

## Оффлайн-сборка

Сеть нужна только на шаге `make sources`. Всё остальное `docker build` берёт
из подложенного `artifacts/` и из apt, который в закрытом контуре ходит через
прокси nexus (`sources.list` подкладывается на машине сборки).

Из репозитория astra ставится всё, что там есть подходящего:
`libestr`, `zlib`, `openssl`, `krb5`, `gnutls`, `libgcrypt`, `libpq`, `mysql`,
`librdkafka`, `hiredis`, `mongoc`, `rabbitmq`, `czmq`, `snmp`, `lognorm`,
`maxminddb`, `systemd`, `zstd`, `jemalloc`, `protobuf-c`, `snappy`, `tcl`,
`pcap`, `net1`, `dbi`, `uuid`, `liblogging-stdlog`.

Из исходников собираются только три вещи:

| | почему не из apt |
|---|---|
| **rsyslog** | нужны заголовки и `config.h` ровно версии `8.2602.0` |
| **libfastjson** | в astra 0.99.8, rsyslog требует `>= 0.99.9` — `configure` останавливается |
| **curl** | пакетный версионирует символы (`CURL_OPENSSL_3`), а в `libcurl.so.4` из RED OS такой версии нет — модуль там не загрузится. Ванильный curl версий не проставляет. Нужен только на линковке, в рантайме берётся системная `libcurl.so.4`, поэтому версия взята заведомо старая |

## Что в образе

`dmp/rsyslog-clickhouse:<версия>` — эталонный `rsyslogd` той же версии поверх
чистого `dmp/glibc:2.28`, собранный с максимумом модулей (97 штук), и рядом
`/opt/rsyslog-ext` с самим модулем. Эталонный rsyslog нужен, чтобы `make test`
проверял загрузку модуля тем же `rsyslogd`, что стоит на целевой машине.

Не собирается — нет библиотеки в репозитории astra:

| модуль | чего не хватает |
|---|---|
| `mbedtls`, `wolfssl` | `libmbedtls`, `libwolfssl` |
| `imhttp` | civetweb |
| `omazureeventhubs` | qpid-proton |
| `imhiredis` | libevent (пакета нет вовсе; `omhiredis` собирается) |
| `ksi-ls12`, `ffaup`, `mmgrok` | `libksi`, `libfaup`, `libgrok` |
| `rfc3195` | `liblogging-rfc3195` |
| `fmhash-xxhash` | `libxxhash` |
| `omhdfs` | `libhdfs` |
| `libcap-ng` | есть 0.7.7, нужно `>= 0.8.2` |
| `imsolaris` | только Solaris |

Отдельно `fmpcre` — не собирается из-за бага апстрима: в 8.2602.0 макросы
`MODULE_TYPE_FUNCTION`/`DEF_FMOD_STATIC_DATA` стали требовать `;`, а этот файл
не поправили.

## Грабли самого модуля

Две вещи, на которые уходит время, если о них не знать (обе учтены в
`conf.d/omclickhouse.conf.example`):

**HTTPS включён по умолчанию.** `usehttps` по умолчанию `on` (и
`allowunsignedcerts` тоже). Сервер без TLS отвечает на такой запрос
`TLS connect error: wrong version number` — надо явно `usehttps="off"`.

**Встроенный шаблон теряет сообщения с обратным слешем.** `" StdClickHouseFmt"`
экранирует по `STDSQL`, то есть только кавычку. ClickHouse понимает и `\` как
экранирующий символ, поэтому сообщение, оканчивающееся на `\`, ломает
`INSERT` — и вся пачка пропадает **молча**: ни записи в журнале, ни строки в
`errorfile`. Лечится своим шаблоном с `option.sql="on"`, который экранирует и
слеш.

## Альтернатива: syslog-ng

`conf.d/syslog-ng-http.conf.example` — то же самое, но если журнал собирает не
rsyslog, а syslog-ng: отправка через штатный `destination http()`. Собирать для
этого ничего не нужно, модули `http` и `json-plugin` входят в `syslog-ng-core`
из репозитория astra (3.19.1). Пишет в ту же таблицу `rsyslog.SystemEvents`,
так что варианты взаимозаменяемы.

Разница по существу: omclickhouse собирает текстовый `INSERT ... VALUES` и
экранирует значения сам (и на STDSQL спотыкается об обратный слеш), а syslog-ng
шлёт `JSONEachRow`, где экранированием занимается `format-json` — кавычки и
слеши там просто не проблема.

Три грабли syslog-ng, все учтены в примере:

* макрос внутри приведения типа пишется **без кавычек**: `int64("$LEVEL_NUM")`
  молча даёт `0`, `int64($LEVEL_NUM)` — число;
* `keep-hostname(yes)` обязателен, иначе в `$HOST` попадёт имя самой машины,
  а не имя из принятой строки syslog;
* в `disk-buffer(reliable(no))` работает `mem-buf-length()` (в сообщениях), а
  `mem-buf-size()` игнорируется с предупреждением. `peer-verify()` в `tls()`
  этой версии принимает только `yes`/`no`.

Пример проверен: `syslog-ng -s` на нём проходит, и с подменённым только
источником 30 строк доехали до ClickHouse — с разобранными `severity`,
`facility`, `timestamp`, сохранённым `hostname` и сообщением с кавычкой,
обратным слешем и двойными кавычками.

## Проверки

`make test` — в образе, на эталонном rsyslogd:

1. `.so` на месте, зависимости разрешаются;
2. модуль грузится и принимает все свои параметры;
3. чужой параметр отвергается — значит параметры разбирает именно модуль;
4. до недоступного сервера модуль доходит через libcurl и сообщает об отказе.

Эталонный rsyslogd — ванильная сборка 8.2602.0, а не пакет `.red80`: если
RED OS патчила структуры rsyslog, тест этого не увидит. Окончательная проверка —
`rsyslogd -N1` на самой машине.
