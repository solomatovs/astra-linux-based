#!/bin/bash
# entrypoint: запуск redis-server с данными в /data
set -euo pipefail

: "${REDIS_DIR:=/data}"

# redis-server без своего конфига — добавляем defaults (dir, bind, protected-mode)
if [ "$1" = "redis-server" ]; then
    shift
    exec redis-server --dir "$REDIS_DIR" --bind '0.0.0.0 -::1' --protected-mode no "$@"
fi

exec "$@"
