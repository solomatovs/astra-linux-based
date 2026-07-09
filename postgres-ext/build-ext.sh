#!/bin/bash
# сборка расширений против установленного postgres (PGXS + cmake для timescaledb)
# аргумент: major-версия postgres. ставит артефакты в DESTDIR=/opt/ext
set -euo pipefail

PG_MAJOR=$1
SRC=/tmp/src
DEST=/opt/ext
PG_CONFIG=$(command -v pg_config)
mkdir -p "$DEST"

log(){ echo ">>> $*"; }
# извлекает тарбол во временную папку и печатает путь к распакованному каталогу
unpack(){ local d; d=$(tar -tf "$SRC/$1" | head -1 | cut -d/ -f1); tar -xf "$SRC/$1" -C /tmp; echo "/tmp/$d"; }
# сборка+установка через PGXS
pgxs(){ local d=$1; shift; make -C "$d" USE_PGXS=1 PG_CONFIG="$PG_CONFIG" "$@"; \
        make -C "$d" USE_PGXS=1 PG_CONFIG="$PG_CONFIG" "$@" install DESTDIR="$DEST"; }

# pgaudit — версия привязана к major
case $PG_MAJOR in
    10) PGAUDIT=1.2.4;; 11) PGAUDIT=1.3.2;; 12) PGAUDIT=1.4.3;;
    13) PGAUDIT=1.5.3;; 14) PGAUDIT=1.6.3;; 15) PGAUDIT=1.7.1;;
    16) PGAUDIT=16.1;;  17) PGAUDIT=17.1;;  18) PGAUDIT=18.0;;  *) PGAUDIT=;;
esac
if [ -n "$PGAUDIT" ]; then
    log "pgaudit $PGAUDIT"
    pgxs "$(unpack "pgaudit-$PGAUDIT.tar.gz")"
fi

# pg_cron (>=10)
if [ "$PG_MAJOR" -ge 10 ]; then
    log "pg_cron 1.6.7"
    pgxs "$(unpack pg_cron-1.6.7.tar.gz)"
fi

# pg_repack (>=10)
if [ "$PG_MAJOR" -ge 10 ]; then
    log "pg_repack 1.5.3"
    pgxs "$(unpack pg_repack-1.5.3.tar.gz)"
fi

# pg_partman (5.x c 14, 4.x до 13)
if [ "$PG_MAJOR" -ge 14 ]; then PARTMAN=5.4.3; else PARTMAN=4.7.4; fi
log "pg_partman $PARTMAN"
pgxs "$(unpack "pg_partman-$PARTMAN.tar.gz")"

# pgvector (>=13)
if [ "$PG_MAJOR" -ge 13 ]; then
    log "pgvector 0.8.5"
    pgxs "$(unpack pgvector-0.8.5.tar.gz)"
fi

# timescaledb (>=15, только apache-лицензия)
if [ "$PG_MAJOR" -ge 15 ]; then
    log "timescaledb 2.28.2 (apache-only)"
    d=$(unpack timescaledb-2.28.2.tar.gz)
    ( cd "$d" \
        && ./bootstrap -DAPACHE_ONLY=ON -DCMAKE_BUILD_TYPE=Release \
            -DREGRESS_CHECKS=OFF -DWARNINGS_AS_ERRORS=OFF -DPG_CONFIG="$PG_CONFIG" \
        && make -C build \
        && make -C build install DESTDIR="$DEST" )
fi

log "установлено в $DEST:"
find "$DEST" -name '*.control' | sort
