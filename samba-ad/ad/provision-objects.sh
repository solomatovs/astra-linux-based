#!/bin/bash
# Идемпотентное наполнение AD по декларативному конфигу.
# Запускается init-хуком entrypoint (когда AD поднят) или вручную.
# Сам ничего «своего» не знает — все объекты описаны в domain-objects.conf
# (рядом со скриптом, либо путь в $OBJECTS_CONF).
#
# Формат конфига (источается как bash) — см. domain-objects.conf:
#   AD_GROUPS=( "имя[:gidNumber]" ... )
#       gidNumber: rfc2307 POSIX gid группы (нужен, если её юзеры ходят на файловые
#                  шары samba-ad); пусто — без POSIX-маппинга.
#   AD_USERS=( "имя:пароль:группа1,группа2[:uidNumber:gidNumber]" ... )
#       uidNumber/gidNumber: rfc2307 POSIX id пользователя. Если заданы — учётке
#       проставляются эти атрибуты и снимается срок действия пароля (--noexpiry),
#       чтобы SMB-клиент (напр. роутер) не отвалился по истечении пароля. Значения
#       ДОЛЖНЫ совпадать с владельцем существующих каталогов на диске, иначе AD-юзер
#       получит авто-uid из idmap-диапазона и не попадёт в свои 0700-каталоги.
#   AD_SERVICES=( "имя:пароль:spn1,spn2:delegation[:keytab]" ... )
#       delegation:
#         ""              — без делегирования
#         "any"           — неограниченное (for-any-service / unconstrained)
#         "to=spn1,spn2"  — ограниченное (constrained), только Kerberos
#         "proto=spn1,..." — ограниченное + protocol transition (for-any-protocol)
#       keytab:     УСТАРЕЛО и ИГНОРИРУЕТСЯ. Провизионер keytab'ы НЕ генерирует —
#                   samba-ad заводит только AD-объекты. keytab делается вручную:
#                     docker exec samba-ad samba-tool domain exportkeytab /tmp/x.keytab \
#                       --principal=HTTP/<spn>.loshara.com@LOSHARA.COM
#                   и кладётся рядом с целевым сервисом.
#   ВНИМАНИЕ: пароль не должен содержать двоеточие ':'.
set -uo pipefail

REALM="${REALM:?REALM не задан}"
OBJECTS_CONF="${OBJECTS_CONF:-$(dirname "$0")/domain-objects.conf}"
SAM_LDB="${SAM_LDB:-/var/lib/samba/private/sam.ldb}"
IDMAP_LDB="${IDMAP_LDB:-/var/lib/samba/private/idmap.ldb}"

log() { echo "[provision-objects] $*"; }

if [ ! -f "${OBJECTS_CONF}" ]; then
    log "конфиг ${OBJECTS_CONF} не найден — нечего создавать"
    exit 0
fi

# ждём готовности AD (на случай прямого запуска вне entrypoint)
log "жду готовности AD ..."
for _ in $(seq 1 90); do
    samba-tool user list >/dev/null 2>&1 && break
    sleep 2
done
if ! samba-tool user list >/dev/null 2>&1; then
    log "AD недоступен — объекты не созданы"
    exit 0
fi

# shellcheck source=/dev/null
. "${OBJECTS_CONF}"

user_exists()     { samba-tool user list     2>/dev/null | grep -qx "$1"; }
group_exists()    { samba-tool group list    2>/dev/null | grep -qx "$1"; }
computer_exists() { samba-tool computer list 2>/dev/null | grep -Fqx "$1\$"; }

# DN объекта по sAMAccountName (пусто, если не найден)
object_dn() { ldbsearch -H "${SAM_LDB}" "(sAMAccountName=$1)" dn 2>/dev/null | sed -n 's/^dn: //p' | head -1; }

# Идемпотентно проставить rfc2307/POSIX-атрибуты объекту.
# Аргументы: sAMAccountName, затем пары «атрибут значение» (напр. uidNumber 2000 gidNumber 2000).
# replace в ldbmodify задаёт значение независимо от наличия атрибута — повторный прогон безопасен.
set_posix_attrs() {
    local name="$1"; shift
    local summary="$*"
    local dn; dn="$(object_dn "${name}")"
    if [ -z "${dn}" ]; then log "WARN: не нашёл DN для ${name} — POSIX-атрибуты пропущены"; return 0; fi
    { echo "dn: ${dn}"; echo "changetype: modify"
      local first=1
      while [ "$#" -ge 2 ]; do
          [ "${first}" -eq 1 ] || echo "-"
          first=0
          echo "replace: $1"; echo "$1: $2"
          shift 2
      done
    } | ldbmodify -H "${SAM_LDB}" >/dev/null 2>&1 \
        && log "posix ${name}: ${summary}" \
        || log "WARN: не удалось задать POSIX-атрибуты для ${name}"
}

# Сбросить возможную авто-аллокацию xid в idmap-кеше для объекта, чтобы rfc2307
# uidNumber/gidNumber стал авторитетным. Если объект впервые резолвится winbind'ом
# ДО простановки POSIX-атрибута, idmap.ldb успевает записать xid из idmap-диапазона
# (~3000000), и он потом перекрывает rfc2307. Удаляем эту запись + чистим кеш.
reset_idmap_for() {
    local name="$1"
    local sid; sid="$(wbinfo --name-to-sid "${name}" 2>/dev/null | awk '{print $1}')"
    [ -n "${sid}" ] || return 0
    ldbdel -H "${IDMAP_LDB}" "CN=${sid}" >/dev/null 2>&1 || true
    net cache flush >/dev/null 2>&1 || true
}

# keytab'ы намеренно НЕ генерируются здесь: samba-ad заводит только AD-объекты,
# а keytab делается вручную по необходимости и кладётся рядом с сервисом
# (см. шапку про поле keytab в AD_SERVICES).

# --- группы: имя[:gidNumber] ---
for spec in "${AD_GROUPS[@]:-}"; do
    [ -n "${spec}" ] || continue
    IFS=: read -r gname gnum <<<"${spec}"
    [ -n "${gname}" ] || continue
    group_exists "${gname}" || { log "group add ${gname}"; samba-tool group add "${gname}"; }
    if [ -n "${gnum}" ]; then set_posix_attrs "${gname}" gidNumber "${gnum}"; reset_idmap_for "${gname}"; fi
done

# --- пользователи: имя:пароль:группы[:uidNumber:gidNumber] ---
for spec in "${AD_USERS[@]:-}"; do
    [ -n "${spec}" ] || continue
    IFS=: read -r name pass groups uidnum gidnum <<<"${spec}"
    user_exists "${name}" || { log "user create ${name}"; samba-tool user create "${name}" "${pass}"; }
    IFS=, read -ra grps <<<"${groups}"
    for grp in "${grps[@]}"; do
        [ -n "${grp}" ] && samba-tool group addmembers "${grp}" "${name}" 2>/dev/null || true
    done
    # rfc2307-маппинг для файловых SMB-пользователей: фиксируем uid/gid под владельца
    # каталогов на диске и снимаем срок действия пароля (иначе истечение = потеря доступа).
    if [ -n "${uidnum}" ]; then
        posix_attrs=(uidNumber "${uidnum}")
        [ -n "${gidnum}" ] && posix_attrs+=(gidNumber "${gidnum}")
        set_posix_attrs "${name}" "${posix_attrs[@]}"
        reset_idmap_for "${name}"
        samba-tool user setexpiry "${name}" --noexpiry >/dev/null 2>&1 || true
    fi
done

# --- сервисные учётки: имя:пароль:spn1,spn2:delegation[:keytab-игнорируется] ---
for spec in "${AD_SERVICES[@]:-}"; do
    [ -n "${spec}" ] || continue
    # 5-е поле (keytab) сохраняем в разборе для совместимости со старым форматом,
    # но НЕ используем — keytab'ы провизионер не генерирует.
    IFS=: read -r name pass spns deleg _keytab_ignored <<<"${spec}"
    user_exists "${name}" || { log "service create ${name}"; samba-tool user create "${name}" "${pass}"; }
    samba-tool user setexpiry "${name}" --noexpiry 2>/dev/null || true

    IFS=, read -ra spn_list <<<"${spns}"
    for spn in "${spn_list[@]}"; do
        [ -n "${spn}" ] || continue
        samba-tool spn list "${name}" 2>/dev/null | grep -q "${spn}" \
            || { log "spn add ${spn} -> ${name}"; samba-tool spn add "${spn}" "${name}"; }
    done

    case "${deleg}" in
        "" )
            : # без делегирования
            ;;
        any )
            log "delegation unconstrained (for-any-service) ${name}"
            samba-tool delegation for-any-service "${name}" on 2>/dev/null || true
            ;;
        to=* | proto=* )
            if [ "${deleg%%=*}" = "proto" ]; then
                log "delegation protocol-transition (for-any-protocol) ${name}"
                samba-tool delegation for-any-protocol "${name}" on 2>/dev/null || true
            fi
            IFS=, read -ra deleg_targets <<<"${deleg#*=}"
            for tgt in "${deleg_targets[@]}"; do
                [ -n "${tgt}" ] || continue
                log "delegation add-service ${tgt} -> ${name}"
                samba-tool delegation add-service "${name}" "${tgt}" 2>/dev/null || true
            done
            ;;
        * )
            log "WARN: неизвестный режим делегирования '${deleg}' для ${name} — пропущен"
            ;;
    esac

done

# --- computer-аккаунты: имя:пароль ---
# Для NTLM-движков (Jespa/EasySSO) сервисная учётка обязана быть computer-аккаунтом
# (sAMAccountName оканчивается на '$'). Пароль выставляется на каждом прогоне —
# идемпотентно гарантирует, что он совпадает с объявленным.
for spec in "${AD_COMPUTERS[@]:-}"; do
    [ -n "${spec}" ] || continue
    IFS=: read -r name pass <<<"${spec}"
    [ -n "${name}" ] || continue
    computer_exists "${name}" || { log "computer create ${name}"; samba-tool computer create "${name}"; }
    log "computer setpassword ${name}\$"
    samba-tool user setpassword "${name}\$" --newpassword="${pass}" >/dev/null 2>&1 \
        || log "WARN: не удалось выставить пароль ${name}\$"
    samba-tool user setexpiry "${name}\$" --noexpiry >/dev/null 2>&1 || true
done

log "готово"
