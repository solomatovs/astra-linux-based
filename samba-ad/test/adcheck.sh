#!/bin/bash
# Смоук-проверка образа samba-ad: версии, python-биндинги samba и
# полный provision AD-домена (без CAP_SYS_ADMIN, xattr в tdb).
set -euo pipefail

echo "== versions =="
samba --version
samba-tool --version
kdb5_util -V 2>/dev/null || true
krb5kdc -h 2>&1 | head -1 || true
python3 --version

echo "== python-биндинги samba =="
python3 -c 'import samba, ldb, talloc; print("samba python OK")'

echo "== provision тестового домена =="
rm -f /etc/samba/smb.conf
samba-tool domain provision \
    --use-rfc2307 \
    --realm=TEST.LOCAL \
    --domain=TEST \
    --server-role=dc \
    --dns-backend=SAMBA_INTERNAL \
    --adminpass='P@ssw0rd123!' \
    --host-name=dc1 \
    --option="vfs objects=dfs_samba4 acl_xattr xattr_tdb" \
    --option="xattr_tdb:file=/var/lib/samba/xattr.tdb"

echo "== проверка результата provision =="
test -f /var/lib/samba/private/sam.ldb
samba-tool domain level show
samba-tool user list
samba-tool user create testuser 'P@ssw0rd123!'
samba-tool user list | grep -q testuser && echo "user create OK"

echo "== ALL OK =="
