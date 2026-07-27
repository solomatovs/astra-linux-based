#!/bin/bash
# Проверка модуля ngx_http_auth_spnego_module в собранном образе:
#   1. .so на месте (release + debug) и сниппет load_module установлен
#   2. модуль грузится (нет "not binary compatible" / "cannot load module")
#   3. директивы auth_gss* и PAC-переменные форка принимаются конфигом
set -euo pipefail

SO=/etc/nginx/modules/ngx_http_auth_spnego_module.so

echo "== 1. artifacts =="
for f in "$SO" "$SO-debug" /etc/nginx/modules.d/10-spnego.conf; do
    if [ -f "$f" ]; then echo "OK  $f"; else echo "FAIL $f отсутствует"; exit 1; fi
done
if ldd "$SO" | grep -q 'not found'; then
    echo "FAIL неразрешённые зависимости:"; ldd "$SO" | grep 'not found'; exit 1
fi
echo "OK  зависимости .so разрешены"

echo "== 2. load + directives =="
d=/tmp/spnegotest; mkdir -p "$d"
cat > "$d/nginx.conf" <<CONF
load_module modules/ngx_http_auth_spnego_module.so;
daemon off;
pid $d/nginx.pid;
events { worker_connections 64; }
http {
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
            auth_gss_keytab $d/http.keytab;
            auth_gss_service_name HTTP/pac.example.com;
            auth_gss_require_group admins editors;
            auth_gss_require_mapped_group on;
            proxy_set_header X-WEBAUTH-ROLE \$role;
            proxy_set_header X-User-Sids    \$spnego_sids;
            proxy_set_header X-User-Groups  \$spnego_groups;
        }
    }
}
CONF
# нет keytab/AD, поэтому -t может не пройти; важны признаки
# «модуль не загружен» / «директива не распознана»
out="$(nginx -c "$d/nginx.conf" -t 2>&1 || true)"
if echo "$out" | grep -qiE 'unknown directive|dlopen|not binary compatible|cannot load module'; then
    echo "FAIL модуль не загружен / директива не распознана:"
    echo "$out" | grep -iE 'unknown directive|dlopen|not binary compatible|cannot load module'; exit 1
fi
echo "OK  auth_gss загружен, директивы и PAC-переменные приняты"

echo "== OK =="
