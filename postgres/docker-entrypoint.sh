#!/bin/bash
# entrypoint: initdb при пустом PGDATA, затем запуск postgres
set -euo pipefail

: "${PGDATA:=/var/lib/postgresql/data}"
: "${POSTGRES_USER:=postgres}"
: "${POSTGRES_PASSWORD:=postgres}"

if [ "$1" = "postgres" ] && [ ! -s "$PGDATA/PG_VERSION" ]; then
    mkdir -p "$PGDATA"
    pwfile="$(mktemp)"
    printf '%s' "$POSTGRES_PASSWORD" > "$pwfile"
    initdb --username="$POSTGRES_USER" --auth=md5 --pwfile="$pwfile" --encoding=UTF8
    rm -f "$pwfile"
    echo "host all all all md5"   >> "$PGDATA/pg_hba.conf"
    echo "listen_addresses = '*'" >> "$PGDATA/postgresql.conf"
fi

exec "$@"
