#!/bin/bash
# сборка динамического модуля ngx_http_auth_spnego_module (Kerberos/SPNEGO + PAC-группы).
# компилируется в дереве nginx из -dist образа теми же аргументами configure,
# что и ядро (configure.args), иначе nginx не примет .so.
# результат — в DESTDIR=/opt/ext, оттуда Dockerfile переносит его в runtime
set -euo pipefail

NGINX_SRC=${NGINX_SRC:-/usr/src/nginx}
MODULE_SRC=${MODULE_SRC:-/usr/src/module}
DEST=${DEST:-/opt/ext}
MODULES_DIR=/etc/nginx/modules

# аргументы ядра: по одному в строке
set --
while IFS= read -r a; do set -- "$@" "$a"; done < "$NGINX_SRC/configure.args"

cd "$NGINX_SRC"

mkdir -p "$DEST$MODULES_DIR" "$DEST/etc/nginx/modules.d" "$DEST/usr/local/lib"

echo ">>> сборка модуля (release)"
./configure "$@" --add-dynamic-module="$MODULE_SRC"
make -j"$(nproc)" modules
cp objs/*.so "$DEST$MODULES_DIR/"

echo ">>> сборка модуля (debug)"
./configure "$@" --with-debug --add-dynamic-module="$MODULE_SRC"
make -j"$(nproc)" modules
for so in objs/*.so; do
    cp "$so" "$DEST$MODULES_DIR/$(basename "$so")-debug"
done

echo ">>> рантайм-библиотеки krb5"
for p in libgssapi_krb5 libkrb5 libk5crypto libcom_err libkrb5support libverto; do
    cp -a /usr/local/lib/$p.so* "$DEST/usr/local/lib/" 2>/dev/null || true
done
cp -a /usr/local/lib/krb5 "$DEST/usr/local/lib/" 2>/dev/null || true

# сниппет load_module
echo 'load_module modules/ngx_http_auth_spnego_module.so;' \
    > "$DEST/etc/nginx/modules.d/10-spnego.conf"

echo ">>> установлено в $DEST:"
find "$DEST$MODULES_DIR" -type f | sort
