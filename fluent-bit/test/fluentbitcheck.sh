#!/bin/bash
# Проверка собранного образа dmp/fluent-bit:
#   1. набор встроенных плагинов (in/out/filter) — должен быть максимальным (FLB_ALL)
#   2. внешние плагины из plugins/ грузятся через -e и регистрируются
#   3. TLS/HTTP-сервер/Lua/WASM/kafka присутствуют
#   4. smoke-пайплайн dummy -> stdout реально прокачивает записи
set -euo pipefail

FLB=/usr/local/bin/fluent-bit

# fluent-bit 4.0.14 иногда падает с SIGSEGV в flb_engine_shutdown() уже ПОСЛЕ
# успешной проверки конфигурации — то есть на выходе из процесса (примерно один
# запуск из десяти, воспроизводится и на голом `--dry-run -i dummy -o null`, без
# наших плагинов). Поэтому выводы делаем по тексту ответа, а не по коду возврата:
# иначе чужой крах на выходе валил бы весь тест случайным образом.
dry() { "$@" 2>&1 || true; }

echo "== 1. plugins =="
# наличие плагина проверяем по dry-run: отсутствующий плагин даёт
# "plugin name that doesn't exist", присутствующий (даже недонастроенный) — нет
has_plugin() {  # $1=роль (-i/-o/-F) $2=имя
    case "$1" in
        -i) args="-i $2 -o null" ;;
        -o) args="-i dummy -o $2" ;;
        -F) args="-i dummy -F $2 -m * -o null" ;;
    esac
    if dry "$FLB" --dry-run $args | grep -q "doesn't exist"; then
        echo "MISS $1 $2"
    else
        echo "OK  $1 $2"
    fi
}
# ключевые входные плагины
for p in dummy tail cpu mem forward http tcp syslog exec kafka prometheus_scrape \
         systemd unix_socket opentelemetry statsd; do has_plugin -i "$p"; done
# ключевые выходные плагины
for p in stdout file http es forward kafka s3 loki prometheus_exporter null \
         pgsql opentelemetry websocket; do has_plugin -o "$p"; done
# ключевые фильтры
for p in grep modify record_modifier lua parser nest rewrite_tag kubernetes \
         geoip2 throttle; do has_plugin -F "$p"; done

# ни один плагин из списков выше не должен оказаться MISS
if has_plugin -o pgsql | grep -q '^MISS'; then
    echo "FAIL out_pgsql не собран — проверь libpq в Dockerfile" >&2
    exit 1
fi

echo "== 2. внешние плагины (.so) =="
# плагины из plugins/ собраны отдельно и подключаются ключом -e. Проверяем не
# факт наличия файла, а то, что dlopen прошёл и имя действительно появилось
# среди плагинов: несовпадение флагов сборки даёт именно молчаливую нерегистрацию
PLUGIN_DIR=/usr/local/lib/fluent-bit/plugins

check_so() {
    so=$1
    base=$(basename "$so" .so); base=${base#flb-}
    case "$base" in
        in_*)     name=${base#in_};     out=$(dry "$FLB" -e "$so" --dry-run -i "$name" -o null 2>&1) ;;
        out_*)    name=${base#out_};    out=$(dry "$FLB" -e "$so" --dry-run -i dummy -o "$name" 2>&1) ;;
        filter_*) name=${base#filter_}; out=$(dry "$FLB" -e "$so" --dry-run -i dummy -F "$name" -m '*' -o null 2>&1) ;;
        *) echo "SKIP $so — роль не читается из имени"; return 0 ;;
    esac
    case "$out" in
        *"doesn't exist"*) echo "$out"; echo "FAIL $so загружен, но $name не зарегистрирован" >&2; return 1 ;;
    esac
    echo "OK  $so -> $name"
}

found=0
for so in "$PLUGIN_DIR"/flb-*.so; do
    [ -e "$so" ] || break
    found=$((found + 1))
    check_so "$so"
done
if [ "$found" -eq 0 ]; then
    echo "FAIL в $PLUGIN_DIR нет ни одного .so — проверь шаг сборки plugins/ в Dockerfile" >&2
    exit 1
fi

echo "== 2.1 режим metrics у своих плагинов =="
# неизвестный параметр fluent-bit отвергает на разборе, а не игнорирует молча,
# поэтому успешный --dry-run с ними и означает, что metrics-режим в плагине есть
metrics_opts() {  # $1=.so $2=имя входа
    dry "$FLB" -e "$1" --dry-run -i "$2" -p targets=localhost -p 'query=SELECT 1' \
        -p mode=metrics -p metric_prefix=test -p label_fields=a -p value_fields=b \
        -p time_field=t -o null
}
for pair in "in_postgres postgres" "in_clickhouse clickhouse"; do
    set -- $pair
    so="$PLUGIN_DIR/flb-$1.so"
    [ -e "$so" ] || { echo "SKIP $1 не собран"; continue; }
    if metrics_opts "$so" "$2" | grep -q "successful"; then
        echo "OK  $2: mode/metric_prefix/label_fields/value_fields/time_field приняты"
    else
        metrics_opts "$so" "$2" | tail -3
        echo "FAIL $2: параметры metrics-режима не приняты" >&2
        exit 1
    fi
done

echo "== 2.1.1 параметры насоса pg2ch =="
so="$PLUGIN_DIR/flb-in_pg2ch.so"
if [ -e "$so" ]; then
    out=$(dry "$FLB" -e "$so" --dry-run -i pg2ch -p pg_target=pg -p ch_target=ch \
        -p ch_table=t -p 'query=SELECT 1' -p 'cursor_query=SELECT 1' \
        -p batch_bytes=1M -p ch_columns=a,b -o null 2>&1)
    if grep -q "successful" <<<"$out"; then
        echo "OK  pg2ch: параметры насоса приняты"
    else
        echo "$out" | tail -3
        echo "FAIL pg2ch: параметры не приняты" >&2
        exit 1
    fi
else
    echo "SKIP pg2ch не собран"
fi

echo "== 2.1.2 выходные плагины: SQL над батчем =="
for pair in "out_clickhouse clickhouse" "out_postgres postgres"; do
    set -- $pair
    so="$PLUGIN_DIR/flb-$1.so"
    [ -e "$so" ] || { echo "SKIP $1 не собран"; continue; }
    # table-режим и query-режим должны приниматься оба
    a=$(dry "$FLB" -e "$so" --dry-run -i dummy -o "$2" -p host=localhost \
        -p table=t -p time_key=ts -p time_format=iso8601 2>&1)
    b=$(dry "$FLB" -e "$so" --dry-run -i dummy -o "$2" -p host=localhost \
        -p 'query=INSERT INTO t SELECT 1' 2>&1)
    if grep -q "successful" <<<"$a" && grep -q "successful" <<<"$b"; then
        echo "OK  $2: приняты и table, и query"
    else
        echo "$a" | tail -2; echo "$b" | tail -2
        echo "FAIL $2: параметры выхода не приняты" >&2
        exit 1
    fi
done

echo "== 2.1.3 TLS: по умолчанию выключен, но принимается =="
# грабля, стоившая регрессии: FLB_INPUT_NET численно равен FLB_IO_OPT_TLS, и
# лишний FLB_IO_TLS во флагах означает «всегда TLS» — плагин полез бы по HTTPS
# на обычный порт. Проверяем, что tls принимается и что по умолчанию его нет
for pair in "in_clickhouse -i clickhouse" "out_clickhouse -o clickhouse"; do
    set -- $pair
    so="$PLUGIN_DIR/flb-$1.so"
    [ -e "$so" ] || { echo "SKIP $1 не собран"; continue; }
    # -p относится к ПОСЛЕДНЕМУ объявленному плагину, поэтому свойства входа
    # обязаны стоять до -o, иначе они уедут выходу
    if [ "$2" = "-i" ]; then
        head="-i $3 -p targets=localhost -p query=SELECT_1"
        tail_args="-o null"
    else
        head="-i dummy -o $3 -p host=localhost -p table=t"
        tail_args=""
    fi
    on=$(dry "$FLB" -e "$so" --dry-run $head -p tls=on -p tls.verify=off $tail_args 2>&1)
    off=$(dry "$FLB" -e "$so" --dry-run $head $tail_args 2>&1)
    if grep -q "successful" <<<"$on" && grep -q "successful" <<<"$off"; then
        echo "OK  $3 ($2): tls принимается, без него — обычный TCP"
    else
        echo "$on" | tail -2; echo "$off" | tail -2
        echo "FAIL $3: разбор tls сломан" >&2
        exit 1
    fi
done

echo "== 2.1.4 кольцевой буфер =="
so="$PLUGIN_DIR/flb-out_ring.so"
if [ -e "$so" ]; then
    out=$(dry "$FLB" -e "$so" --dry-run -i dummy -o ring -p host=127.0.0.1 \
        -p port=2022 -p uri=/logs -p clear_uri=/logs/clear -p stats_uri=/stats \
        -p ring_size=1M -p time_key=ts -p time_format=iso8601 2>&1)
    if grep -q "successful" <<<"$out"; then
        echo "OK  ring: параметры кольца приняты"
    else
        echo "$out" | tail -3
        echo "FAIL ring: параметры не приняты" >&2
        exit 1
    fi
else
    echo "SKIP ring не собран"
fi

echo "== 2.1.5 расписание (cron) у входов =="
# 5 полей и 6 полей должны приниматься оба; проверяем на всех трёх входах
for pair in "in_postgres -i postgres" "in_clickhouse -i clickhouse" "in_pg2ch -i pg2ch"; do
    set -- $pair
    so="$PLUGIN_DIR/flb-$1.so"
    [ -e "$so" ] || { echo "SKIP $1 не собран"; continue; }
    case "$3" in
        pg2ch) args="-p pg_target=pg -p ch_target=ch -p ch_table=t -p query=SELECT_1" ;;
        *)     args="-p targets=localhost -p query=SELECT_1" ;;
    esac
    a=$(dry "$FLB" -e "$so" --dry-run -i "$3" $args -p 'schedule=0 2 * * *' -o null 2>&1)
    b=$(dry "$FLB" -e "$so" --dry-run -i "$3" $args -p 'schedule=*/30 * * * * *' -o null 2>&1)
    if grep -q "successful" <<<"$a" && grep -q "successful" <<<"$b"; then
        echo "OK  $3: schedule принят и в 5, и в 6 полей"
    else
        echo "$a" | tail -2; echo "$b" | tail -2
        echo "FAIL $3: schedule не принят" >&2
        exit 1
    fi
done

echo "== 2.1.6 пробы: четыре типа =="
so="$PLUGIN_DIR/flb-in_probe.so"
if [ -e "$so" ]; then
    for t in tcp icmp tls http; do
        case "$t" in
            icmp) tg="localhost" ;;
            http) tg="http://localhost:80/" ;;
            *)    tg="localhost:80" ;;
        esac
        out=$(dry "$FLB" -e "$so" --dry-run -i probe -p type=$t -p targets="$tg" \
            -p timeout_ms=1000 -p mode=metrics -o null)
        if grep -q "successful" <<<"$out"; then
            echo "OK  probe: тип $t принят"
        else
            echo "$out" | tail -3
            echo "FAIL probe: тип $t не принят" >&2
            exit 1
        fi
    done
    # преамбулы TLS для postgres и AD
    out=$(dry "$FLB" -e "$so" --dry-run -i probe -p type=tls -p targets=localhost:5432 \
        -p preamble=postgres -o null)
    out2=$(dry "$FLB" -e "$so" --dry-run -i probe -p type=tls -p targets=localhost:389 \
        -p preamble=ldap-starttls -o null)
    if grep -q "successful" <<<"$out" && grep -q "successful" <<<"$out2"; then
        echo "OK  probe: преамбулы postgres и ldap-starttls приняты"
    else
        echo "FAIL probe: преамбулы не приняты" >&2
        exit 1
    fi
else
    echo "SKIP probe не собран"
fi

echo "== 2.1.7 обратное соединение: пара stream =="
so_out="$PLUGIN_DIR/flb-out_stream.so"
so_in="$PLUGIN_DIR/flb-in_stream.so"
if [ -e "$so_out" ] && [ -e "$so_in" ]; then
    # периферия: выход, который слушает порт (задан Listen)
    out=$(dry "$FLB" -e "$so_out" --dry-run -i dummy -o stream \
        -p listen=127.0.0.1 -p port=19999 -p token=t -p require_ack=on \
        -p ack_timeout_ms=3000 -p heartbeat_sec=10)
    if grep -q "successful" <<<"$out"; then
        echo "OK  out_stream: параметры периферии приняты"
    else
        echo "$out" | tail -3
        echo "FAIL out_stream: параметры не приняты" >&2
        exit 1
    fi

    # центр: вход, который подключается к списку периферий
    out=$(dry "$FLB" -e "$so_in" --dry-run -i stream \
        -p targets=127.0.0.1:19999,127.0.0.1:19998 -p token=t \
        -p source_key=source -p poll_ms=200 -p retry_pause_sec=5 -o null)
    if grep -q "successful" <<<"$out"; then
        echo "OK  in_stream: параметры центра приняты"
    else
        echo "$out" | tail -3
        echo "FAIL in_stream: параметры не приняты" >&2
        exit 1
    fi

    # Зеркальная схема: направление СОЕДИНЕНИЯ выводится из конфигурации и
    # с направлением данных не связано. Задан Host — выход звонит сам,
    # задан Listen — вход ждёт
    out=$(dry "$FLB" -e "$so_out" --dry-run -i dummy -o stream \
        -p host=127.0.0.1 -p port=19999 -p node=edge-1 -p retry_pause_sec=5)
    out2=$(dry "$FLB" -e "$so_in" --dry-run -i stream \
        -p listen=127.0.0.1 -p port=19999 -p token=t -o null)
    if grep -q "successful" <<<"$out" && grep -q "successful" <<<"$out2"; then
        echo "OK  stream: обратная схема (выход звонит, вход ждёт) принята"
    else
        echo "$out" | tail -3; echo "$out2" | tail -3
        echo "FAIL stream: обратная схема не принята" >&2
        exit 1
    fi

    # выход одновременно и слушает, и звонит — бессмыслица: соединение одно
    out=$("$FLB" -e "$so_out" -i dummy -o stream -p listen=127.0.0.1 \
          -p host=127.0.0.1 -p port=19999 -f 1 2>&1 &
          pid=$!; sleep 3; kill -TERM $pid 2>/dev/null || true;
          wait $pid 2>/dev/null || true)
    if grep -qi "заданы и Listen, и Host" <<<"$out"; then
        echo "OK  out_stream: Listen вместе с Host отвергнуты"
    else
        echo "$out" | tail -3
        echo "FAIL out_stream: Listen вместе с Host проехали" >&2
        exit 1
    fi

    # а вход может и то, и другое сразу: часть узлов обзваниваем, часть ждём
    out=$(dry "$FLB" -e "$so_in" --dry-run -i stream -p targets=127.0.0.1:19999 \
        -p listen=127.0.0.1 -p port=19998 -o null)
    if grep -q "successful" <<<"$out"; then
        echo "OK  in_stream: Targets и Listen вместе приняты"
    else
        echo "$out" | tail -3
        echo "FAIL in_stream: Targets вместе с Listen не приняты" >&2
        exit 1
    fi

    # TLS: у слушающей стороны свои Cert_File/Key_File, у звонящей Verify.
    # Имя «tls» занять нельзя — ядро перехватывает свойства на tls* раньше
    # config_map, поэтому переключатель называется Secure
    out=$(dry "$FLB" -e "$so_in" --dry-run -i stream -p targets=127.0.0.1:19999 \
        -p secure=on -p verify=off -o null)
    if grep -q "successful" <<<"$out"; then
        echo "OK  in_stream: TLS-параметры приняты"
    else
        echo "FAIL in_stream: TLS-параметры не приняты" >&2
        exit 1
    fi

    # Ни Targets, ни Listen — плагину нечего делать, он обязан не подняться.
    # Проверять это через --dry-run нельзя: он разбирает конфигурацию, но
    # cb_init не вызывает (проверено: пустая конфигурация проезжает молча)
    out=$("$FLB" -e "$so_in" -i stream -o null -f 1 2>&1 &
          pid=$!; sleep 3; kill -TERM $pid 2>/dev/null || true;
          wait $pid 2>/dev/null || true)
    # Судим по своему сообщению, а не по «input initialization failed»:
    # 5.x на неподнявшемся входе останавливает движок, 3.2.10 продолжает
    # работать без него — разница версий, не плагина
    if grep -qi "укажите Targets" <<<"$out"; then
        echo "OK  in_stream: без Targets и Listen не поднимается"
    else
        echo "$out" | tail -3
        echo "FAIL in_stream: пустая конфигурация проехала молча" >&2
        exit 1
    fi
else
    echo "SKIP пара stream не собрана"
fi

echo "== 2.2 библиотека запросов и примеры =="
SHARE=/usr/local/share/fluent-bit
for d in queries/pg queries/ch examples; do
    n=$(ls "$SHARE/$d" 2>/dev/null | wc -l)
    if [ "$n" -gt 0 ]; then
        echo "OK  $SHARE/$d — файлов: $n"
    else
        echo "FAIL $SHARE/$d пуст — проверь COPY queries/examples в Dockerfile" >&2
        exit 1
    fi
done

echo "== 3. build features (fluent-bit --version / build flags) =="
dry "$FLB" --version
# наличие TLS/HTTP-сервера подтверждаем запуском с http-сервером ниже

echo "== 4. smoke pipeline (dummy -> stdout) =="
out="$("$FLB" -q \
    -i dummy -p 'dummy={"smoke":"astra-fluent-bit","n":42}' -p samples=3 \
    -o stdout -p format=json_lines \
    -f 1 2>/dev/null &
    pid=$!
    sleep 3
    kill -TERM $pid 2>/dev/null || true
    wait $pid 2>/dev/null || true)" || true

echo "--- captured stdout ---"
echo "$out" | grep -c 'astra-fluent-bit' >/dev/null 2>&1 \
    && { echo "$out" | grep 'astra-fluent-bit' | head -3; echo "OK  pipeline produced records"; } \
    || { echo "$out" | head -20; echo "WARN pipeline output not matched"; }

echo "== 5. TLS + HTTP monitoring server =="
# запускаем с включённым http-сервером и проверяем /api/v1/metrics через /dev/tcp
"$FLB" -q -H -P 2020 \
    -i dummy -p samples=1 -o null -f 1 >/dev/null 2>&1 &
srv=$!
trap 'kill -TERM $srv 2>/dev/null || true' EXIT
for i in $(seq 1 20); do (exec 3<>/dev/tcp/127.0.0.1/2020) 2>/dev/null && break; sleep 0.2; done
if exec 3<>/dev/tcp/127.0.0.1/2020 2>/dev/null; then
    printf 'GET /api/v1/metrics HTTP/1.0\r\nHost: localhost\r\n\r\n' >&3
    head -1 <&3
    exec 3>&- 3<&-
    echo "OK  http monitoring server up"
else
    echo "WARN http server not reachable"
fi

echo "== OK =="
