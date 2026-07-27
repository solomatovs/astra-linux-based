#!/bin/bash
# проверка pgvector в собранном образе
set -euo pipefail

PGDATA=/tmp/pgtest-pgvector
SOCK=/tmp
export PGDATA

echo "== $(postgres --version) : pgvector =="

rm -rf "$PGDATA"
initdb -U postgres -A trust --encoding=UTF8 >/dev/null

pg_ctl -D "$PGDATA" -o "-c listen_addresses=''" -w start
trap 'pg_ctl -D "$PGDATA" -m fast stop >/dev/null 2>&1 || true' EXIT

q(){ psql -h "$SOCK" -U postgres -v ON_ERROR_STOP=1 "$@"; }

q -c 'CREATE EXTENSION vector;'
echo "версия: $(q -Atc "SELECT extversion FROM pg_extension WHERE extname='vector';")"

echo "l2 distance: $(q -Atc "SELECT '[1,2,3]'::vector <-> '[1,2,4]'::vector;")"

q -v ON_ERROR_STOP=1 <<'SQL'
CREATE TABLE vec_probe (id serial PRIMARY KEY, emb vector(3));
INSERT INTO vec_probe (emb) VALUES ('[1,2,3]'), ('[4,5,6]'), ('[7,8,9]');
CREATE INDEX ON vec_probe USING hnsw (emb vector_l2_ops);
SELECT id FROM vec_probe ORDER BY emb <-> '[3,3,3]' LIMIT 1;
SQL

echo "== OK =="
