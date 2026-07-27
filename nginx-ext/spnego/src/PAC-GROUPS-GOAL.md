# Цель разработки: PAC-группы в spnego-модуле nginx

Форк `spnego-http-auth-nginx-module`, цель — извлекать группы пользователя из
Kerberos-тикета (PAC) и отдавать их наружу для авторизации на бэкендах
(первый потребитель — Grafana OSS 11).

## Итоговая цель

nginx с этим модулем при SPNEGO/Kerberos-логине против AD:
1. проверяет тикет по keytab (как сейчас) и выдаёт `$remote_user`;
2. достаёт из PAC тикета SID групп пользователя;
3. отдаёт их в переменные nginx и умеет фильтровать доступ по группам;
4. на основе групп бэкенд получает роль/членство (для Grafana — роль через заголовок).

## Контракт (зафиксировано)

**Декод PAC:** вариант (a′) — БЕЗ samba. PAC достаём через MIT krb5 (уже слинкован):
`krb5_pac_parse()` + `krb5_pac_get_buffer(KRB5_PAC_LOGON_INFO)` → сырой
`KERB_VALIDATION_INFO`, который разбираем собственным компактным NDR-ридером
(MS-PAC §2.5): `LogonDomainId` (SID домена) + `GroupIds[]`/`PrimaryGroupId` (RID)
+ опц. `ExtraSids[]` → полный SID = `<domainSID>-<RID>`.
Причина отказа от (b): `libndr-krb5pac` тянет ~34 .so (пол-samba + icu + gnutls),
+30–40 МБ и apt-зависимости в рантайм — ломает минимализм. Вариант (a′) —
~150–250 строк C, ноль новых runtime-.so, образ остаётся ~149 МБ.
PAC из GSSAPI-контекста берём через `gss_get_name_attribute("urn:mspac:logon-info")`
(или полный `urn:mspac:` + krb5_pac_parse), т.к. модуль работает через GSSAPI.

**Директивы** (контексты `http/server/location` + `limit_except`, хранение в
loc-conf, merge = self-contained/replace: набор берётся из того контекста, где
задан; при отсутствии — наследуется от родителя):

| Директива | Повторяемая | Смысл |
|---|---|---|
| `auth_gss_group_sid <name> <SID>` | да | запись словаря SID→имя в текущем контексте |
| `auth_gss_require_group <name> [<name>...]` | да | **OR**: 403, если нет ни одной из перечисленных групп |
| `auth_gss_require_mapped_group on\|off` | нет | 403, если ни один SID пользователя не замаплен |

**Переменные** (значение — по конфигу того location, куда попал запрос):
- `$spnego_sids` — сырые SID пользователя, **через запятую**;
- `$spnego_groups` — отмапленные имена групп, **через запятую**.

**Разделитель — запятая**: совместимо с `X-WEBAUTH-GROUPS` Grafana и удобно для `map`.

**Фаза:** решение о доступе (403) — на access-фазе, до `proxy_pass`.

**Модуль остаётся generic:** «группа→роль» не зашивается в C; для Grafana роль
собирается в nginx через `map $spnego_groups $role`.

## Интеграция с Grafana OSS 11 (важно)

- **Team Sync через `X-WEBAUTH-GROUPS` — Enterprise-only.** В community 11 группы
  в команды не мапятся. Поэтому для доступа используем **роль**.
- OSS потребляет: `X-WEBAUTH-USER` (логин), `X-WEBAUTH-NAME/EMAIL`,
  **`X-WEBAUTH-ROLE`** (`Viewer`/`Editor`/`Admin`) — поддерживается в OSS.
- Схема: модуль отдаёт `$spnego_groups` → nginx `map` → `X-WEBAUTH-ROLE`.
- `X-WEBAUTH-USER` = `$remote_user` без реалма (`auth_gss_map_to_local on`).
- grafana.ini: `[auth.proxy] enabled=true; header_name=X-WEBAUTH-USER;
  headers=Role:X-WEBAUTH-ROLE; whitelist=<IP nginx>`.

## Тестовый стенд (уже работает)

Сеть docker `docker` (external), оба контейнера в ней.

**samba-ad** (`/app/docker/compose/samba-ad`, образ `dmp/samba-ad:4.21.3`):
- realm `LOSHARA.COM`, домен `LOSHARA`, DC `dc1` @ `172.18.0.13`
- **SID домена:** `S-1-5-21-2440191825-2527142554-1554121245`
- группы: `boba-DEV` (RID 1103), `boba-ADM` (1104), `boba-READ` (1109), `confluence-users`
- пользователи: `solomatovs` (boba-DEV, boba-ADM), `readonly` (boba-READ)
- SPN/keytab добавляются в `local/domain-objects.conf` → `AD_SERVICES`
  (шаблон `'name:pass:HTTP/host::/out/name.keytab'`), keytab пишется в `local/`.
  Для теста нужен SPN `HTTP/<nginx-host>.loshara.com` + keytab.

**grafana** (`/app/docker/compose/grafana`, `grafana/grafana:11.6.3-ubuntu`):
- @ `172.18.0.27:3000`, admin `solomatovs`
- `[auth.proxy]` сейчас `enabled = false` — включить для теста.

## План проверки (acceptance)

1. Завести в samba-ad SPN `HTTP/<nginx>.loshara.com` + keytab, ре-провижн.
2. Запустить наш `dmp/nginx` в сети `docker` с `krb5.conf` (LOSHARA.COM) и keytab,
   reverse-proxy на `172.18.0.27:3000`.
3. Включить `[auth.proxy]` в grafana.ini (whitelist = IP nginx).
4. location: `auth_gss on` + `auth_gss_group_sid` для boba-ADM/DEV/READ +
   `auth_gss_require_group` + `map $spnego_groups $role` + заголовки.
5. С клиента получить тикет (`kinit` + `curl --negotiate`) и проверить:
   - SPNEGO ок, `$remote_user = solomatovs`;
   - `$spnego_sids`/`$spnego_groups` содержат boba-ADM,boba-DEV;
   - роль замаплена (boba-ADM → Admin), Grafana логинит solomatovs как Admin;
   - `readonly` → boba-READ → Viewer;
   - пользователь вне разрешённых групп → 403 до Grafana.

## NDR-раскладка KERB_VALIDATION_INFO (ВЫВЕРЕНО на реальном PAC solomatovs)

Буфер `urn:mspac:logon-info` = type-serialization v1:
- 8 байт common header (`01 10 08 00 cc cc cc cc`) + 8 байт private header
  (`ObjectBufferLength`(4) + filler(4)). NDR-поток начинается с байта 16;
  **выравнивание считается относительно байта 16**.
- 4 байта top unique-pointer referent, затем struct **упакован (без 8-align)**.

Фиксированная часть (по порядку): 6×FILETIME(8); 6×RPC_UNICODE_STRING
(Len2,Max2,ptr4); LogonCount2,BadPwd2; UserId4,PrimaryGroupId4,GroupCount4,
GroupIds.ptr4; UserFlags4; UserSessionKey16; LogonServer(str8),LogonDomainName(str8);
LogonDomainId.ptr4; Reserved1[2]8; UserAccountControl4,SubAuthStatus4;
Last{Successful,Failed}ILogon 2×8; FailedILogonCount4,Reserved3 4;
SidCount4,ExtraSids.ptr4; ResourceGroupDomainSid.ptr4,ResourceGroupCount4,
ResourceGroupIds.ptr4. (fixed_end = 236 для этого сэмпла)

**Deferred-референты — строго в порядке появления указателей**, читать только ptr≠0:
1–6) буферы юникод-строк: align4, MaxCount4,Offset4,ActualCount4, затем
   ActualCount×2 байт, align4;
7) GroupIds: align4, MaxCount4, затем MaxCount×(RelativeId4,Attributes4) → RID'ы;
8–9) LogonServer/LogonDomainName буферы (str);
10) LogonDomainId → SID: align4, MaxCount4(=SubAuthCount), Rev1,SubAuthCount1,
   IdAuth6(BE), SubAuth×4; полный SID группы = `<domainSID>-<RID>`;
11) ExtraSids: align4, MaxCount4, MaxCount×(Sid.ptr4,Attr4), затем для каждого
   ненулевого — SID-референт (как выше);
12–13) ResourceGroupDomainSid (sid) + ResourceGroupIds (grouparr) — по надобности.

Референс-реализация и сэмпл: `scratchpad/pac/parse.py` + `logon-info.hex`.

## Открытые вопросы / на потом
- Сборка: подключить samba `libndr`+gen_ndr в dist-стадию (headers + .so в рантайм),
  минимизировать зависимость (только ndr, не весь samba).
- Формат нескольких SPN/keytab, если nginx обслуживает несколько host.
