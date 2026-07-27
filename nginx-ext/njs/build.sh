#!/bin/bash
# сборка ngx_http_js_module и ngx_stream_js_module.
# сначала движок njs (libnjs.a), затем модули — в дереве nginx из -dist образа
# теми же аргументами configure, что и ядро (configure.args), иначе nginx не примет .so.
# результат — в DESTDIR=/opt/ext, оттуда Dockerfile переносит его в runtime
set -euo pipefail

NGINX_SRC=${NGINX_SRC:-/usr/src/nginx}
NJS_SRC=${NJS_SRC:-/usr/src/njs}
DEST=${DEST:-/opt/ext}
MODULES_DIR=/etc/nginx/modules

# без webcrypto/xml/zlib/quickjs: pcre и openssl модулям даёт сам nginx
export NJS_OPENSSL=NO NJS_LIBXSLT=NO NJS_ZLIB=NO NJS_QUICKJS=NO

echo ">>> сборка движка njs (libnjs.a)"
cd "$NJS_SRC"
./configure --cc=gcc --no-openssl --no-libxml2 --no-zlib --no-quickjs --no-pcre
make -j"$(nproc)" libnjs

# аргументы ядра: по одному в строке
set --
while IFS= read -r a; do set -- "$@" "$a"; done < "$NGINX_SRC/configure.args"

cd "$NGINX_SRC"

mkdir -p "$DEST$MODULES_DIR" "$DEST/etc/nginx/modules.d"

echo ">>> сборка модулей (release)"
./configure "$@" --add-dynamic-module="$NJS_SRC/nginx"
make -j"$(nproc)" modules
cp objs/*.so "$DEST$MODULES_DIR/"

echo ">>> сборка модулей (debug)"
./configure "$@" --with-debug --add-dynamic-module="$NJS_SRC/nginx"
make -j"$(nproc)" modules
for so in objs/*.so; do
    cp "$so" "$DEST$MODULES_DIR/$(basename "$so")-debug"
done

# сниппет load_module. stream-модуль закомментирован: ему нужен блок stream{}
cat > "$DEST/etc/nginx/modules.d/30-njs.conf" <<'CONF'
load_module modules/ngx_http_js_module.so;
# для stream-контекста раскомментируйте (нужен блок stream {} в nginx.conf):
# load_module modules/ngx_stream_js_module.so;
CONF

echo ">>> установлено в $DEST:"
find "$DEST$MODULES_DIR" -type f | sort
