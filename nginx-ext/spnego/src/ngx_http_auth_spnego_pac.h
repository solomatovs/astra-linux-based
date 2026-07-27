/*
 * Мини-NDR парсер KERB_VALIDATION_INFO (MS-PAC §2.5) из буфера
 * urn:mspac:logon-info. Извлекает SID групп пользователя. Не зависит от nginx —
 * чистый C, бонусно юнит-тестируется (см. ngx_http_auth_spnego_pac.c, PAC_TEST).
 * Вход недоверенный (из тикета) — все чтения проверяются по границам буфера.
 */
#ifndef NGX_HTTP_AUTH_SPNEGO_PAC_H
#define NGX_HTTP_AUTH_SPNEGO_PAC_H

#include <stddef.h>

/*
 * Колбэк на каждый извлечённый SID (строка вида "S-1-5-21-...-<rid>",
 * NUL-терминирована, len — без NUL). Возврат 0 — продолжать, !=0 — прервать.
 */
typedef int (*spnego_pac_sid_cb)(void *cbctx, const char *sid, size_t len);

/*
 * Разобрать logon-info blob; на каждый SID группы (из GroupIds, домен-SID + RID)
 * и каждый ExtraSid вызвать cb. Возврат: 0 — успех, <0 — ошибка разбора.
 */
int ngx_spnego_pac_extract_sids(const unsigned char *buf, size_t len,
                                spnego_pac_sid_cb cb, void *cbctx);

#endif /* NGX_HTTP_AUTH_SPNEGO_PAC_H */
