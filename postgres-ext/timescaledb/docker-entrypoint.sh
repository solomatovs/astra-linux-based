#!/bin/bash
# entrypoint образа: initdb при пустом PGDATA + preload timescaledb
set -euo pipefail

: "${PGDATA:=/var/lib/postgresql/data}"
: "${POSTGRES_USER:=postgres}"
: "${POSTGRES_PASSWORD:=postgres}"
: "${TIMESCALEDB_TELEMETRY:=off}"

if [ "$1" = "postgres" ] && [ ! -s "$PGDATA/PG_VERSION" ]; then
    mkdir -p "$PGDATA"
    pwfile="$(mktemp)"
    printf '%s' "$POSTGRES_PASSWORD" > "$pwfile"
    initdb --username="$POSTGRES_USER" --auth=md5 --pwfile="$pwfile" --encoding=UTF8
    rm -f "$pwfile"
    echo "host all all all md5" >> "$PGDATA/pg_hba.conf"
    {
        echo "listen_addresses = '*'"
        echo "shared_preload_libraries = 'timescaledb'"
        echo "timescaledb.telemetry_level = $TIMESCALEDB_TELEMETRY"
    } >> "$PGDATA/postgresql.conf"
fi

exec "$@"
