#!/bin/bash
# Проверка собранного образа dmp/pgsimcity:
#   1. бандл на месте (index.html + ассеты vite + wasm PGlite)
#   2. в index.html нет аналитики plausible.io (вырезана при сборке)
#   3. nginx стартует, отдает index с ожидаемым <title>
#   4. /healthz (stub_status), Content-Type js и wasm, SPA-fallback (try_files)
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
wasm=$(find $ROOT -name '*.wasm' -print -quit)
test -n "$wasm"
echo "OK  index.html + $(find $ROOT -type f | wc -l) файлов бандла, wasm: ${wasm#$ROOT}"

echo "== 2. no plausible.io =="
! grep -r 'plausible\.io' $ROOT/index.html
echo "OK  аналитика вырезана, страница не ходит наружу"

echo "== 3. nginx start =="
nginx -t
nginx
for i in $(seq 1 20); do (exec 3<>/dev/tcp/127.0.0.1/80) 2>/dev/null && break; sleep 0.2; done

echo "== 4. GET / =="
resp=$(http_get /)
echo "$resp" | head -1
echo "$resp" | head -1 | grep -q ' 200 '
echo "$resp" | grep -q 'PGSimCity'
echo "OK  index отдается, title на месте"

echo "== 5. asset Content-Type =="
head=$(http_head "${js#$ROOT}")
echo "$head" | head -1 | grep -q ' 200 '
echo "$head" | grep -qi 'Content-Type: \(application\|text\)/javascript'
echo "OK  js отдается с правильным Content-Type"

echo "== 6. wasm Content-Type (PGlite) =="
head=$(http_head "${wasm#$ROOT}")
echo "$head" | head -1 | grep -q ' 200 '
echo "$head" | grep -qi 'Content-Type: application/wasm'
echo "OK  wasm отдается как application/wasm"

echo "== 7. /healthz (stub_status) =="
http_get /healthz | grep -q 'Active connections'
echo "OK  /healthz"

echo "== 8. SPA fallback =="
http_get /no/such/route | grep -q 'PGSimCity'
echo "OK  неизвестный путь отдает index.html (try_files)"

echo "== OK =="
