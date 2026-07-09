#!/bin/bash
# проверка образа: версии, initdb, старт сервера, sql, contrib
set -euo pipefail

PGDATA=/tmp/pgtest
SOCK=/tmp
export PGDATA

echo "== versions =="
postgres --version
initdb  --version
psql    --version
pg_config --version
echo "configure: $(pg_config --configure)"

echo "== initdb + start =="
rm -rf "$PGDATA"
initdb -U postgres -A trust --encoding=UTF8 >/dev/null
# сокет по умолчанию /tmp (сборка из исходников); tcp отключаем
pg_ctl -D "$PGDATA" -o "-c listen_addresses=''" -w start

trap 'pg_ctl -D "$PGDATA" -m fast stop >/dev/null 2>&1 || true' EXIT

psql -h "$SOCK" -U postgres -Atc 'SELECT version();'

echo "== ddl/dml =="
psql -h "$SOCK" -U postgres -v ON_ERROR_STOP=1 <<'SQL'
CREATE TABLE t (id serial PRIMARY KEY, name text);
INSERT INTO t (name) VALUES ('foo'), ('bar');
SELECT count(*) AS rows FROM t;
SQL

echo "== contrib =="
vernum=$(psql -h "$SOCK" -U postgres -Atc 'SHOW server_version_num')
if [ "$vernum" -ge 90100 ]; then
    # CREATE EXTENSION появился в 9.1
    psql -h "$SOCK" -U postgres -v ON_ERROR_STOP=1 -c 'CREATE EXTENSION hstore;'
    psql -h "$SOCK" -U postgres -Atc "SELECT ('a=>1'::hstore -> 'a');"
    psql -h "$SOCK" -U postgres -v ON_ERROR_STOP=1 -c 'CREATE EXTENSION pgcrypto;'
    psql -h "$SOCK" -U postgres -Atc "SELECT encode(digest('astra','sha256'),'hex');"
    # uuid-ossp только для сборок с --with-uuid (9.4+)
    if psql -h "$SOCK" -U postgres -c 'CREATE EXTENSION "uuid-ossp";' >/dev/null 2>&1; then
        psql -h "$SOCK" -U postgres -Atc 'SELECT uuid_generate_v4();'
        echo "uuid-ossp: OK"
    else
        echo "uuid-ossp: недоступно (9.0-9.3)"
    fi
else
    # 9.0: CREATE EXTENSION нет — contrib грузится sql-скриптом
    psql -h "$SOCK" -U postgres -v ON_ERROR_STOP=1 -q -f "$(pg_config --sharedir)/contrib/hstore.sql"
    psql -h "$SOCK" -U postgres -Atc "SELECT ('a=>1'::hstore -> 'a');"
fi

echo "== pl languages =="
# plperl/pltcl — все версии; plpython3u — 10+
if [ "$vernum" -ge 90100 ]; then
    psql -h "$SOCK" -U postgres -v ON_ERROR_STOP=1 -c 'CREATE EXTENSION plperl;'
    psql -h "$SOCK" -U postgres -v ON_ERROR_STOP=1 -c 'CREATE EXTENSION pltcl;'
else
    psql -h "$SOCK" -U postgres -v ON_ERROR_STOP=1 -c 'CREATE LANGUAGE plperl;'
    psql -h "$SOCK" -U postgres -v ON_ERROR_STOP=1 -c 'CREATE LANGUAGE pltcl;'
fi
psql -h "$SOCK" -U postgres -v ON_ERROR_STOP=1 \
    -c "CREATE FUNCTION plperl_max(int,int) RETURNS int AS \$\$ return \$_[0] > \$_[1] ? \$_[0] : \$_[1]; \$\$ LANGUAGE plperl;"
psql -h "$SOCK" -U postgres -Atc 'SELECT plperl_max(3,7);'
echo "plperl/pltcl: OK"
if [ "$vernum" -ge 100000 ]; then
    psql -h "$SOCK" -U postgres -v ON_ERROR_STOP=1 -c 'CREATE EXTENSION plpython3u;'
    psql -h "$SOCK" -U postgres -Atc "DO \$\$ plpy.notice('plpython3u works') \$\$ LANGUAGE plpython3u;" >/dev/null
    echo "plpython3u: OK"
fi

echo "== OK =="
