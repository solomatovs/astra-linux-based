#!/bin/bash
# проверка pg_probackup: версия + init каталога + FULL --stream бэкап работающего сервера
set -euo pipefail

PGDATA=/tmp/pgtest
CAT=/tmp/backup
SOCK=/tmp
export PGDATA

echo "== version =="
pg_probackup --version

echo "== запуск postgres =="
rm -rf "$PGDATA" "$CAT"
initdb -U postgres -A trust --encoding=UTF8 >/dev/null
# для stream-бэкапа нужен wal_level>=replica (в PG10+ по умолчанию) и слоты
pg_ctl -D "$PGDATA" -o "-c listen_addresses='' -c max_wal_senders=4 -c wal_level=replica" -w start
trap 'pg_ctl -D "$PGDATA" -m fast stop >/dev/null 2>&1 || true' EXIT

psql -h "$SOCK" -U postgres -v ON_ERROR_STOP=1 -c 'CREATE TABLE t AS SELECT generate_series(1,1000) AS id;'

echo "== init + add-instance =="
pg_probackup init -B "$CAT"
pg_probackup add-instance -B "$CAT" -D "$PGDATA" --instance main

echo "== FULL --stream backup =="
pg_probackup backup -B "$CAT" --instance main -b FULL --stream \
    -d postgres -h "$SOCK" -U postgres

echo "== show =="
pg_probackup show -B "$CAT" --instance main
pg_probackup show -B "$CAT" --instance main | grep -q OK && echo "backup status: OK"

echo "== OK =="
