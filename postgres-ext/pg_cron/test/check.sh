#!/bin/bash
# проверка pg_cron в собранном образе
set -euo pipefail

PGDATA=/tmp/pgtest-pg_cron
SOCK=/tmp
export PGDATA

echo "== $(postgres --version) : pg_cron =="

rm -rf "$PGDATA"
initdb -U postgres -A trust --encoding=UTF8 >/dev/null

# pg_cron стартует только из preload
{
    echo "shared_preload_libraries = 'pg_cron'"
    echo "cron.database_name = 'postgres'"
} >> "$PGDATA/postgresql.conf"

pg_ctl -D "$PGDATA" -o "-c listen_addresses=''" -w start
trap 'pg_ctl -D "$PGDATA" -m fast stop >/dev/null 2>&1 || true' EXIT

q(){ psql -h "$SOCK" -U postgres -v ON_ERROR_STOP=1 "$@"; }

q -c 'CREATE EXTENSION pg_cron;'
echo "версия: $(q -Atc "SELECT extversion FROM pg_extension WHERE extname='pg_cron';")"

jobid=$(q -Atc "SELECT cron.schedule('0 0 * * *', 'SELECT 1');")
echo "cron.schedule -> job $jobid"
q -Atc "SELECT cron.unschedule($jobid);" >/dev/null

echo "== OK =="
