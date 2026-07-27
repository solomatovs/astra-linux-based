#!/bin/bash
# Проверка модулей njs в собранном образе:
#   1. .so на месте (release + debug) и сниппет load_module установлен
#   2. модули грузятся (нет "not binary compatible" / "cannot load module")
#   3. js_import/js_content реально исполняют JS: HTTP-ответ отдаёт njs-обработчик
set -euo pipefail

HTTP_SO=/etc/nginx/modules/ngx_http_js_module.so
STREAM_SO=/etc/nginx/modules/ngx_stream_js_module.so

echo "== 1. artifacts =="
for f in "$HTTP_SO" "$HTTP_SO-debug" "$STREAM_SO" "$STREAM_SO-debug" \
         /etc/nginx/modules.d/30-njs.conf; do
    if [ -f "$f" ]; then echo "OK  $f"; else echo "FAIL $f отсутствует"; exit 1; fi
done
for so in "$HTTP_SO" "$STREAM_SO"; do
    if ldd "$so" | grep -q 'not found'; then
        echo "FAIL неразрешённые зависимости $so:"; ldd "$so" | grep 'not found'; exit 1
    fi
done
echo "OK  зависимости .so разрешены"

echo "== 2. load + JS execution =="
d=/tmp/njstest; mkdir -p "$d"
cat > "$d/http.js" <<'JS'
function hello(r) {
    r.return(200, 'njs ok ' + njs.version);
}
export default { hello };
JS

cat > "$d/nginx.conf" <<CONF
load_module modules/ngx_http_js_module.so;
load_module modules/ngx_stream_js_module.so;
daemon off;
pid $d/nginx.pid;
error_log stderr notice;
events { worker_connections 64; }
http {
    access_log off;
    client_body_temp_path $d/tmp;
    js_import main from $d/http.js;
    server {
        listen 127.0.0.1:8082;
        location /js { js_content main.hello; }
    }
}
stream {
    js_import smain from $d/http.js;
}
CONF

nginx -c "$d/nginx.conf" -t
nginx -c "$d/nginx.conf" &
pid=$!
trap 'kill -QUIT $pid 2>/dev/null || true' EXIT

for i in $(seq 1 20); do (exec 3<>/dev/tcp/127.0.0.1/8082) 2>/dev/null && break; sleep 0.2; done

# HTTP-запрос через bash /dev/tcp (в рантайме нет curl)
exec 3<>/dev/tcp/127.0.0.1/8082
printf 'GET /js HTTP/1.0\r\nHost: localhost\r\n\r\n' >&3
resp="$(cat <&3)"
exec 3>&- 3<&-

echo "$resp"
if echo "$resp" | grep -q 'njs ok'; then
    echo "OK  js_content исполнил JS-обработчик"
else
    echo "FAIL njs-обработчик не отработал"; exit 1
fi

echo "== OK =="
