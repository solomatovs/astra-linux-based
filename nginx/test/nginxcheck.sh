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
         --with-stream --with-mail --with-pcre-jit         \
         spnego-http-auth-nginx-module nginx-auth-ldap; do
    if echo "$V" | grep -q -- "$m"; then echo "OK  $m"; else echo "MISS $m"; fi
done
# http_v3 есть не во всех версиях (nginx >= 1.25)
if echo "$V" | grep -q -- http_v3_module; then echo "OK  http_v3_module"; else echo "n/a http_v3_module"; fi

# директивы kerberos/ldap должны приниматься конфигом (модули реально слинкованы)
echo "== 1b. auth directives =="
adir=/tmp/authtest; mkdir -p "$adir"
cat > "$adir/nginx.conf" <<CONF
daemon off;
pid $adir/nginx.pid;
events { worker_connections 64; }
http {
    ldap_server ad {
        url "ldap://ad.example.com/DC=example,DC=com?sAMAccountName?sub";
        binddn "cn=svc,dc=example,dc=com";
        binddn_passwd secret;
        group_attribute member;
        require valid_user;
    }
    # PAC-группы (форк): словарь SID->имя + map группы->роль
    auth_gss_group_sid admins  S-1-5-21-1-2-3-512;
    auth_gss_group_sid editors S-1-5-21-1-2-3-1104;
    map \$spnego_groups \$role {
        default Viewer;
        "~(^|,)admins(,|\$)"  Admin;
        "~(^|,)editors(,|\$)" Editor;
    }
    server {
        listen 127.0.0.1:8081;
        server_name pac.example.com;
        location /gss {
            auth_gss on;
            auth_gss_realm EXAMPLE.COM;
            auth_gss_keytab $adir/http.keytab;
            auth_gss_service_name HTTP/pac.example.com;
            auth_gss_require_group admins editors;
            auth_gss_require_mapped_group on;
            proxy_set_header X-WEBAUTH-ROLE \$role;
            proxy_set_header X-User-Sids    \$spnego_sids;
            proxy_set_header X-User-Groups  \$spnego_groups;
        }
        location /ldap {
            auth_ldap "restricted";
            auth_ldap_servers ad;
        }
    }
}
CONF
# нет keytab/AD, поэтому -t может не пройти; важен лишь признак «модуль не слинкован»
out="$(nginx -c "$adir/nginx.conf" -t 2>&1 || true)"
if echo "$out" | grep -q 'unknown directive'; then
    echo "FAIL: директива не распознана (модуль не слинкован):"; echo "$out" | grep 'unknown directive'; exit 1
fi
echo "OK  auth_gss / auth_ldap directives parsed"

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
