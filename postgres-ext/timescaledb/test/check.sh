#!/bin/bash
# проверка timescaledb в собранном образе
set -euo pipefail

PGDATA=/tmp/pgtest-timescaledb
SOCK=/tmp
export PGDATA

echo "== $(postgres --version) : timescaledb =="

rm -rf "$PGDATA"
initdb -U postgres -A trust --encoding=UTF8 >/dev/null

# timescaledb работает только из preload
{
    echo "shared_preload_libraries = 'timescaledb'"
    echo "timescaledb.telemetry_level = off"
} >> "$PGDATA/postgresql.conf"

pg_ctl -D "$PGDATA" -o "-c listen_addresses=''" -w start
trap 'pg_ctl -D "$PGDATA" -m fast stop >/dev/null 2>&1 || true' EXIT

q(){ psql -h "$SOCK" -U postgres -v ON_ERROR_STOP=1 "$@"; }

q -c 'CREATE EXTENSION timescaledb;'
echo "версия: $(q -Atc "SELECT extversion FROM pg_extension WHERE extname='timescaledb';")"

q -v ON_ERROR_STOP=1 <<'SQL'
CREATE TABLE ts (t timestamptz NOT NULL, v double precision);
SELECT create_hypertable('ts', 't');
INSERT INTO ts VALUES (now(), 1), (now(), 2);
SELECT count(*) FROM ts;
SQL

echo "== OK =="
