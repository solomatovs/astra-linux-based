#!/bin/bash
# проверка pgaudit в собранном образе
set -euo pipefail

PGDATA=/tmp/pgtest-pgaudit
SOCK=/tmp
export PGDATA

echo "== $(postgres --version) : pgaudit =="

rm -rf "$PGDATA"
initdb -U postgres -A trust --encoding=UTF8 >/dev/null

# pgaudit работает только из preload
{
    echo "shared_preload_libraries = 'pgaudit'"
    echo "pgaudit.log = 'write, ddl'"
} >> "$PGDATA/postgresql.conf"

pg_ctl -D "$PGDATA" -o "-c listen_addresses=''" -w start
trap 'pg_ctl -D "$PGDATA" -m fast stop >/dev/null 2>&1 || true' EXIT

q(){ psql -h "$SOCK" -U postgres -v ON_ERROR_STOP=1 "$@"; }

q -c 'CREATE EXTENSION pgaudit;'
echo "версия: $(q -Atc "SELECT extversion FROM pg_extension WHERE extname='pgaudit';")"

q -c 'CREATE TABLE audit_probe(i int);'
q -c 'INSERT INTO audit_probe VALUES (1);'
q -Atc 'SELECT count(*) FROM audit_probe;'

echo "== OK =="
