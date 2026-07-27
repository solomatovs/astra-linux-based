#!/bin/bash
# проверка pg_partman в собранном образе
set -euo pipefail

PGDATA=/tmp/pgtest-pg_partman
SOCK=/tmp
export PGDATA

echo "== $(postgres --version) : pg_partman =="

rm -rf "$PGDATA"
initdb -U postgres -A trust --encoding=UTF8 >/dev/null

pg_ctl -D "$PGDATA" -o "-c listen_addresses=''" -w start
trap 'pg_ctl -D "$PGDATA" -m fast stop >/dev/null 2>&1 || true' EXIT

q(){ psql -h "$SOCK" -U postgres -v ON_ERROR_STOP=1 "$@"; }

q -c 'CREATE SCHEMA partman; CREATE EXTENSION pg_partman SCHEMA partman;'
echo "версия: $(q -Atc "SELECT extversion FROM pg_extension WHERE extname='pg_partman';")"

# сигнатуры create_parent различаются в 4.x и 5.x, поэтому проверяем только наличие API
n=$(q -Atc "SELECT count(DISTINCT p.proname) FROM pg_proc p JOIN pg_namespace n ON n.oid = p.pronamespace
            WHERE n.nspname = 'partman' AND p.proname IN ('create_parent','run_maintenance');")
[ "$n" = "2" ] || { echo "нет partman.create_parent / run_maintenance"; exit 1; }
echo "partman.create_parent / run_maintenance на месте"

q -c "SELECT partman.run_maintenance();"

echo "== OK =="
