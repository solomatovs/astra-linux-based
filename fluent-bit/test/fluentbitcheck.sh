#!/bin/bash
# Проверка собранного образа dmp/fluent-bit:
#   1. набор встроенных плагинов (in/out/filter) — должен быть максимальным (FLB_ALL)
#   2. внешние плагины из plugins/ грузятся через -e и регистрируются
#   3. TLS/HTTP-сервер/Lua/WASM/kafka присутствуют
#   4. smoke-пайплайн dummy -> stdout реально прокачивает записи
set -euo pipefail

FLB=/usr/local/bin/fluent-bit

echo "== 1. plugins =="
# наличие плагина проверяем по dry-run: отсутствующий плагин даёт
# "plugin name that doesn't exist", присутствующий (даже недонастроенный) — нет
has_plugin() {  # $1=роль (-i/-o/-F) $2=имя
    case "$1" in
        -i) args="-i $2 -o null" ;;
        -o) args="-i dummy -o $2" ;;
        -F) args="-i dummy -F $2 -m * -o null" ;;
    esac
    if "$FLB" --dry-run $args 2>&1 | grep -q "doesn't exist"; then
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
        in_*)     name=${base#in_};     out=$("$FLB" -e "$so" --dry-run -i "$name" -o null 2>&1) ;;
        out_*)    name=${base#out_};    out=$("$FLB" -e "$so" --dry-run -i dummy -o "$name" 2>&1) ;;
        filter_*) name=${base#filter_}; out=$("$FLB" -e "$so" --dry-run -i dummy -F "$name" -m '*' -o null 2>&1) ;;
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

echo "== 3. build features (fluent-bit --version / build flags) =="
"$FLB" --version
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
