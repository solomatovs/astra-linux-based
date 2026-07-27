#!/bin/bash
# сборка pg_partman против установленного postgres (PGXS), артефакты в DESTDIR=/opt/ext
set -euo pipefail

SRC=/tmp/src
DEST=/opt/ext
PG_CONFIG=$(command -v pg_config)

mkdir -p "$SRC" "$DEST"
tar -xf /tmp/src.tar.gz -C "$SRC" --strip-components=1

make -C "$SRC" USE_PGXS=1 PG_CONFIG="$PG_CONFIG"
make -C "$SRC" USE_PGXS=1 PG_CONFIG="$PG_CONFIG" install DESTDIR="$DEST"

echo ">>> установлено в $DEST:"
find "$DEST" -name '*.control' | sort
