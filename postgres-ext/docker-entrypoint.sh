#!/bin/bash
# entrypoint -ext образа: initdb при пустом PGDATA + прописывание shared_preload_libraries
# (pgaudit/pg_cron/timescaledb требуют preload). Список задаётся PG_EXT_PRELOAD (по major).
set -euo pipefail

: "${PGDATA:=/var/lib/postgresql/data}"
: "${POSTGRES_USER:=postgres}"
: "${POSTGRES_PASSWORD:=postgres}"
: "${POSTGRES_DB:=postgres}"
: "${PG_EXT_PRELOAD:=pgaudit,pg_cron}"

if [ "$1" = "postgres" ] && [ ! -s "$PGDATA/PG_VERSION" ]; then
    mkdir -p "$PGDATA"
    pwfile="$(mktemp)"
    printf '%s' "$POSTGRES_PASSWORD" > "$pwfile"
    initdb --username="$POSTGRES_USER" --auth=md5 --pwfile="$pwfile" --encoding=UTF8
    rm -f "$pwfile"
    echo "host all all all md5" >> "$PGDATA/pg_hba.conf"
    {
        echo "listen_addresses = '*'"
        echo "shared_preload_libraries = '$PG_EXT_PRELOAD'"
        echo "cron.database_name = '$POSTGRES_DB'"
        echo "timescaledb.telemetry_level = off"
    } >> "$PGDATA/postgresql.conf"
fi

exec "$@"
