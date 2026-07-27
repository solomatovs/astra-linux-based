#!/bin/bash
# проверка pg_repack в собранном образе
set -euo pipefail

PGDATA=/tmp/pgtest-pg_repack
SOCK=/tmp
export PGDATA

echo "== $(postgres --version) : pg_repack =="

rm -rf "$PGDATA"
initdb -U postgres -A trust --encoding=UTF8 >/dev/null

pg_ctl -D "$PGDATA" -o "-c listen_addresses=''" -w start
trap 'pg_ctl -D "$PGDATA" -m fast stop >/dev/null 2>&1 || true' EXIT

q(){ psql -h "$SOCK" -U postgres -v ON_ERROR_STOP=1 "$@"; }

q -c 'CREATE EXTENSION pg_repack;'
echo "версия: $(q -Atc "SELECT extversion FROM pg_extension WHERE extname='pg_repack';")"
pg_repack --version

# pg_repack требует первичный или уникальный ключ
q -c 'CREATE TABLE repack_probe(i int PRIMARY KEY, v text);'
q -c "INSERT INTO repack_probe SELECT g, 'x' FROM generate_series(1,1000) g;"
q -c 'DELETE FROM repack_probe WHERE i % 2 = 0;'
pg_repack -h "$SOCK" -U postgres -d postgres --table public.repack_probe --no-superuser-check
echo "строк после repack: $(q -Atc 'SELECT count(*) FROM repack_probe;')"

echo "== OK =="
