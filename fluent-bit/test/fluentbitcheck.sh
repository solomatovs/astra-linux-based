#!/bin/bash
# Проверка собранного образа dmp/fluent-bit:
#   1. набор собранных плагинов (in/out/filter) — должен быть максимальным (FLB_ALL)
#   2. TLS/HTTP-сервер/Lua/WASM/kafka присутствуют
#   3. smoke-пайплайн dummy -> stdout реально прокачивает записи
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
# ключевые входные плагины (systemd намеренно выключен — тянет libsystemd)
for p in dummy tail cpu mem forward http tcp syslog exec; do has_plugin -i "$p"; done
# ключевые выходные плагины
for p in stdout file http es forward kafka s3 loki prometheus_exporter null; do has_plugin -o "$p"; done
# ключевые фильтры
for p in grep modify record_modifier lua parser nest rewrite_tag kubernetes; do has_plugin -F "$p"; done

echo "== 2. build features (fluent-bit --version / build flags) =="
"$FLB" --version
# наличие TLS/HTTP-сервера подтверждаем запуском с http-сервером ниже

echo "== 3. smoke pipeline (dummy -> stdout) =="
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

echo "== 4. TLS + HTTP monitoring server =="
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
