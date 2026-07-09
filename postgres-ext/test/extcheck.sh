#!/bin/bash
# проверка расширений в -ext образе. аргумент: major-версия postgres
set -euo pipefail

PG_MAJOR=$1
PGDATA=/tmp/pgtest
SOCK=/tmp
export PGDATA

echo "== $(postgres --version) (major $PG_MAJOR) =="

rm -rf "$PGDATA"
initdb -U postgres -A trust --encoding=UTF8 >/dev/null

# pgaudit/timescaledb/pg_cron требуют shared_preload_libraries (timescaledb должен быть первым)
if [ "$PG_MAJOR" -ge 15 ]; then PRELOAD="timescaledb,pgaudit,pg_cron"; else PRELOAD="pgaudit,pg_cron"; fi
{
    echo "shared_preload_libraries = '$PRELOAD'"
    echo "cron.database_name = 'postgres'"
    echo "timescaledb.telemetry_level = off"
} >> "$PGDATA/postgresql.conf"

pg_ctl -D "$PGDATA" -o "-c listen_addresses=''" -w start
trap 'pg_ctl -D "$PGDATA" -m fast stop >/dev/null 2>&1 || true' EXIT

q(){ psql -h "$SOCK" -U postgres -v ON_ERROR_STOP=1 "$@"; }

echo "== pgaudit =="
q -c 'CREATE EXTENSION pgaudit;'
q -Atc "SELECT extversion FROM pg_extension WHERE extname='pgaudit';"

echo "== pg_cron =="
q -c 'CREATE EXTENSION pg_cron;'
q -Atc "SELECT cron.schedule('0 0 * * *', 'SELECT 1');" >/dev/null && echo "cron.schedule OK"

echo "== pg_repack =="
q -c 'CREATE EXTENSION pg_repack;'
pg_repack --version

echo "== pg_partman =="
q -c 'CREATE SCHEMA partman; CREATE EXTENSION pg_partman SCHEMA partman;'
q -Atc "SELECT extversion FROM pg_extension WHERE extname='pg_partman';"

if [ "$PG_MAJOR" -ge 13 ]; then
    echo "== pgvector =="
    q -c 'CREATE EXTENSION vector;'
    q -Atc "SELECT '[1,2,3]'::vector <-> '[1,2,4]'::vector;"
fi

if [ "$PG_MAJOR" -ge 15 ]; then
    echo "== timescaledb =="
    q -c 'CREATE EXTENSION timescaledb;'
    q -v ON_ERROR_STOP=1 <<'SQL'
CREATE TABLE ts (t timestamptz NOT NULL, v double precision);
SELECT create_hypertable('ts', 't');
INSERT INTO ts VALUES (now(), 1), (now(), 2);
SELECT count(*) FROM ts;
SQL
fi

echo "== установленные расширения =="
q -Atc "SELECT extname||' '||extversion FROM pg_extension ORDER BY extname;"

echo "== OK =="
