#!/bin/bash
# Проверка модуля ngx_http_auth_ldap_module в собранном образе:
#   1. .so на месте (release + debug) и сниппет load_module установлен
#   2. модуль грузится (нет "not binary compatible" / "cannot load module")
#   3. директивы ldap_server / auth_ldap принимаются конфигом
set -euo pipefail

SO=/etc/nginx/modules/ngx_http_auth_ldap_module.so

echo "== 1. artifacts =="
for f in "$SO" "$SO-debug" /etc/nginx/modules.d/20-auth-ldap.conf; do
    if [ -f "$f" ]; then echo "OK  $f"; else echo "FAIL $f отсутствует"; exit 1; fi
done
if ldd "$SO" | grep -q 'not found'; then
    echo "FAIL неразрешённые зависимости:"; ldd "$SO" | grep 'not found'; exit 1
fi
echo "OK  зависимости .so разрешены"

echo "== 2. load + directives =="
d=/tmp/ldaptest; mkdir -p "$d"
cat > "$d/nginx.conf" <<CONF
load_module modules/ngx_http_auth_ldap_module.so;
daemon off;
pid $d/nginx.pid;
events { worker_connections 64; }
http {
    ldap_server ad {
        url "ldap://ad.example.com/DC=example,DC=com?sAMAccountName?sub";
        binddn "cn=svc,dc=example,dc=com";
        binddn_passwd secret;
        group_attribute member;
        require valid_user;
    }
    server {
        listen 127.0.0.1:8081;
        location /ldap {
            auth_ldap "restricted";
            auth_ldap_servers ad;
        }
    }
}
CONF
# AD недоступен, поэтому -t может не пройти; важны признаки
# «модуль не загружен» / «директива не распознана»
out="$(nginx -c "$d/nginx.conf" -t 2>&1 || true)"
if echo "$out" | grep -qiE 'unknown directive|dlopen|not binary compatible|cannot load module'; then
    echo "FAIL модуль не загружен / директива не распознана:"
    echo "$out" | grep -iE 'unknown directive|dlopen|not binary compatible|cannot load module'; exit 1
fi
echo "OK  auth_ldap загружен, директивы приняты"

echo "== OK =="
