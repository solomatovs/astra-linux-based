#!/bin/bash
# Проверка собранного образа dmp/nginx:
#   1. набор собранных модулей (http_ssl/v2/v3, stream, mail, pcre-jit ...)
#   2. запуск nginx в фоне на 127.0.0.1:8080 + HTTP-ответ (bash /dev/tcp, без curl)
#   3. stub_status (подтверждает http_stub_status_module)
set -euo pipefail

echo "== 1. modules =="
V="$(nginx -V 2>&1)"
for m in http_ssl_module http_v2_module http_realip_module \
         http_gzip_static_module http_stub_status_module   \
         --with-stream --with-mail --with-pcre-jit; do
    if echo "$V" | grep -q -- "$m"; then echo "OK  $m"; else echo "MISS $m"; fi
done
# http_v3 есть не во всех версиях (nginx >= 1.25)
if echo "$V" | grep -q -- http_v3_module; then echo "OK  http_v3_module"; else echo "n/a http_v3_module"; fi

# HTTP-запрос через bash /dev/tcp (в рантайме нет curl)
http_get() {
    exec 3<>"/dev/tcp/127.0.0.1/8080"
    printf 'GET %s HTTP/1.0\r\nHost: localhost\r\n\r\n' "$1" >&3
    cat <&3
    exec 3>&- 3<&-
}

echo "== 2. start + HTTP =="
root=/tmp/nginxtest
mkdir -p "$root/html" "$root/tmp"
echo 'astra nginx ok' > "$root/html/index.html"

cat > "$root/nginx.conf" <<CONF
daemon off;
pid $root/nginx.pid;
error_log stderr notice;
events { worker_connections 64; }
http {
    access_log off;
    client_body_temp_path $root/tmp;
    server {
        listen 127.0.0.1:8080;
        root $root/html;
        location = /healthz { stub_status; }
    }
}
CONF

nginx -c "$root/nginx.conf" -t
nginx -c "$root/nginx.conf" &
pid=$!
trap 'kill -QUIT $pid 2>/dev/null || true' EXIT

for i in $(seq 1 20); do (exec 3<>/dev/tcp/127.0.0.1/8080) 2>/dev/null && break; sleep 0.2; done

echo "--- GET / ---"
http_get /

echo "== 3. stub_status =="
http_get /healthz

echo "== OK =="
