#!/bin/bash
# Проверка собранного образа dmp/chsimcity:
#   1. бандл на месте (index.html + ассеты vite)
#   2. nginx стартует, отдает index с ожидаемым <title>
#   3. /healthz (stub_status), Content-Type ассетов, SPA-fallback (try_files)
# curl'а в рантайме нет — HTTP через bash /dev/tcp
set -euo pipefail

ROOT=/etc/nginx/html

http_get() {
    exec 3<>/dev/tcp/127.0.0.1/80
    printf 'GET %s HTTP/1.0\r\nHost: localhost\r\n\r\n' "$1" >&3
    cat <&3
    exec 3>&- 3<&-
}

http_head() {
    exec 3<>/dev/tcp/127.0.0.1/80
    printf 'HEAD %s HTTP/1.0\r\nHost: localhost\r\n\r\n' "$1" >&3
    cat <&3
    exec 3>&- 3<&-
}

echo "== 1. files =="
test -f $ROOT/index.html
js=$(find $ROOT/assets -name '*.js' -print -quit)
test -n "$js"
echo "OK  index.html + $(find $ROOT -type f | wc -l) файлов бандла"

echo "== 2. nginx start =="
nginx -t
nginx
for i in $(seq 1 20); do (exec 3<>/dev/tcp/127.0.0.1/80) 2>/dev/null && break; sleep 0.2; done

echo "== 3. GET / =="
resp=$(http_get /)
echo "$resp" | head -1
echo "$resp" | head -1 | grep -q ' 200 '
echo "$resp" | grep -q 'CHSimCity'
echo "OK  index отдается, title на месте"

echo "== 4. asset Content-Type =="
head=$(http_head "${js#$ROOT}")
echo "$head" | head -1 | grep -q ' 200 '
echo "$head" | grep -qi 'Content-Type: \(application\|text\)/javascript'
echo "OK  js отдается с правильным Content-Type"

echo "== 5. /healthz (stub_status) =="
http_get /healthz | grep -q 'Active connections'
echo "OK  /healthz"

echo "== 6. SPA fallback =="
http_get /no/such/route | grep -q 'CHSimCity'
echo "OK  неизвестный путь отдает index.html (try_files)"

echo "== OK =="
