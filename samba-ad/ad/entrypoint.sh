#!/bin/bash
# Чистый AD DC: первый старт — provision домена (только базовые объекты),
# дальше просто поднимаем samba.
#
# Прикладных пользователей/групп/SPN образ НЕ создаёт. Чтобы донаполнить домен,
# смонтируй свои скрипты в ${INIT_DIR} (по умолчанию /docker-entrypoint-init.d)
# через docker-compose — они выполнятся на старте, когда AD поднимется
# (пиши их идемпотентно: создавай объекты только если их ещё нет).
set -euo pipefail

# REALM/DOMAIN/DC_HOSTNAME/ADMIN_PASSWORD/DNS_FORWARDER нужны ТОЛЬКО при первом
# provision (ниже). После него всё хранится в базе AD, и на последующих стартах
# эти переменные не требуются — поэтому их не объявляем тут (иначе set -u
# роняет контейнер при каждом запуске без них).
PRIVATE_DIR=/var/lib/samba/private
SMB_CONF=/etc/samba/smb.conf
INIT_DIR="${INIT_DIR:-/docker-entrypoint-init.d}"

log() { echo "[entrypoint] $*"; }

# В контейнере запись security.NTACL xattr (нужна для ACL sysvol) упирается в
# отсутствие CAP_SYS_ADMIN. Поэтому храним xattr в tdb (vfs xattr_tdb) — provision
# проходит без привилегий, эти же опции пишутся в smb.conf и для демона.
XATTR_OPTS=(
    --option="vfs objects=dfs_samba4 acl_xattr xattr_tdb"
    --option="xattr_tdb:file=/var/lib/samba/xattr.tdb"
)

provision() {
    : "${REALM:?нужен при первом provision}" \
      "${DOMAIN:?нужен при первом provision}" \
      "${DC_HOSTNAME:?нужен при первом provision}" \
      "${ADMIN_PASSWORD:?нужен при первом provision}" \
      "${DNS_FORWARDER:?нужен при первом provision}"
    log "provision домена ${REALM} (NetBIOS ${DOMAIN}, host ${DC_HOSTNAME}) ..."
    rm -f "${SMB_CONF}"
    samba-tool domain provision \
        --use-rfc2307 \
        --realm="${REALM}" \
        --domain="${DOMAIN}" \
        --server-role=dc \
        --dns-backend=SAMBA_INTERNAL \
        --adminpass="${ADMIN_PASSWORD}" \
        --host-name="${DC_HOSTNAME}" \
        "${XATTR_OPTS[@]}"

    cp -f "${PRIVATE_DIR}/krb5.conf" /etc/krb5.conf

    # форвардер для разрешения внешних имён внутренним DNS samba
    if grep -q 'dns forwarder' "${SMB_CONF}"; then
        sed -i "s|.*dns forwarder.*|\tdns forwarder = ${DNS_FORWARDER}|" "${SMB_CONF}"
    else
        sed -i "/\[global\]/a\\\tdns forwarder = ${DNS_FORWARDER}" "${SMB_CONF}"
    fi

    # Разрешаем подключение по simple bind по 389
    if ! grep -q 'ldap server require strong auth' "${SMB_CONF}"; then
        sed -i "/\[global\]/a\\\tldap server require strong auth = no" "${SMB_CONF}"
    fi

    log "provision завершён"
}

# Выполнить примонтированные init-скрипты (если есть), когда AD поднимется.
run_init_hooks() {
    [ -d "${INIT_DIR}" ] || { log "init-каталог ${INIT_DIR} не смонтирован — чистый DC"; return 0; }
    set -- "${INIT_DIR}"/*.sh
    [ -e "$1" ] || { log "init-скриптов нет — чистый DC"; return 0; }

    log "жду готовности AD для init-скриптов ..."
    for _ in $(seq 1 90); do
        samba-tool user list >/dev/null 2>&1 && break
        sleep 2
    done
    if ! samba-tool user list >/dev/null 2>&1; then
        log "AD не поднялся за отведённое время — init-скрипты пропущены"
        return 0
    fi

    for s in "${INIT_DIR}"/*.sh; do
        [ -e "$s" ] || continue
        log "init: ${s}"
        bash "$s" || log "init ${s} завершился с ошибкой (продолжаю)"
    done
    log "init завершён"
}

if [ ! -f "${PRIVATE_DIR}/sam.ldb" ]; then
    provision
else
    log "домен уже provision'ен — пропускаю"
    [ -f "${PRIVATE_DIR}/krb5.conf" ] && cp -f "${PRIVATE_DIR}/krb5.conf" /etc/krb5.conf || true
fi

# init-хуки в фоне, когда samba поднимется
run_init_hooks &

log "запускаю samba (AD DC)"
exec samba --foreground --no-process-group
