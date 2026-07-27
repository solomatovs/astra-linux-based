#!/bin/bash
# сборка timescaledb против установленного postgres (cmake), артефакты в DESTDIR=/opt/ext
set -euo pipefail

SRC=/tmp/src
DEST=/opt/ext
PG_CONFIG=$(command -v pg_config)

mkdir -p "$SRC" "$DEST"
tar -xf /tmp/src.tar.gz -C "$SRC" --strip-components=1

cd "$SRC"
./bootstrap                             \
    -DAPACHE_ONLY=ON                    \
    -DCMAKE_BUILD_TYPE=Release          \
    -DREGRESS_CHECKS=OFF                \
    -DWARNINGS_AS_ERRORS=OFF            \
    -DPG_CONFIG="$PG_CONFIG"
make -C build -j"$(nproc)"
make -C build install DESTDIR="$DEST"

echo ">>> установлено в $DEST:"
find "$DEST" -name '*.control' | sort
