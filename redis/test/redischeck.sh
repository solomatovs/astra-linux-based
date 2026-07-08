#!/bin/bash
# проверка образа: версии, старт сервера, ping/set/get
set -euo pipefail

DIR=/tmp/redistest
SOCK=/tmp/redis-test.sock

echo "== versions =="
redis-server    --version
redis-cli       --version
redis-benchmark --version

echo "== start =="
rm -rf "$DIR"; mkdir -p "$DIR"
# tcp отключаем (--port 0), общаемся через unix-сокет
redis-server --daemonize no --port 0 --unixsocket "$SOCK" --dir "$DIR" --save '' &
srv=$!
trap 'kill "$srv" >/dev/null 2>&1 || true' EXIT

for i in $(seq 1 50); do
    [ -S "$SOCK" ] && redis-cli -s "$SOCK" ping >/dev/null 2>&1 && break
    sleep 0.2
done

echo "== ping =="
redis-cli -s "$SOCK" ping

echo "== set/get =="
redis-cli -s "$SOCK" set foo bar
test "$(redis-cli -s "$SOCK" get foo)" = "bar"

echo "== info =="
redis-cli -s "$SOCK" info server | grep -E 'redis_version|multiplexing_api'

echo "== OK =="
