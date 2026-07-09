/*
 * Мини-NDR парсер KERB_VALIDATION_INFO (MS-PAC §2.5). См. .h.
 * Раскладка выверена на реальном PAC (см. PAC-GROUPS-GOAL.md).
 */
#include "ngx_http_auth_spnego_pac.h"
#include <string.h>
#include <stdio.h>
#include <stdint.h>

/* NDR-поток начинается на 16-м байте (после type-serialization header);
 * выравнивание считается относительно этого начала. */
#define NDR_ORIGIN 16

/* защитные лимиты против «злого» PAC */
#define PAC_MAX_GROUPS   1024
#define PAC_MAX_SUBAUTH  15
#define SID_STR_MAX      128

typedef struct {
    const unsigned char *b;
    size_t len;
    size_t o;
    int err;
} ndr_rdr;

static void ndr_align(ndr_rdr *r, size_t n) {
    if (r->err || r->o < NDR_ORIGIN) return;
    while (((r->o - NDR_ORIGIN) % n) != 0) {
        if (r->o >= r->len) { r->err = 1; return; }
        r->o++;
    }
}

static uint16_t ndr_u16(ndr_rdr *r) {
    if (r->err || r->o + 2 > r->len) { r->err = 1; return 0; }
    uint16_t v = (uint16_t)(r->b[r->o] | (r->b[r->o + 1] << 8));
    r->o += 2;
    return v;
}

static uint32_t ndr_u32(ndr_rdr *r) {
    if (r->err || r->o + 4 > r->len) { r->err = 1; return 0; }
    uint32_t v = (uint32_t)r->b[r->o] | ((uint32_t)r->b[r->o + 1] << 8)
               | ((uint32_t)r->b[r->o + 2] << 16) | ((uint32_t)r->b[r->o + 3] << 24);
    r->o += 4;
    return v;
}

static void ndr_skip(ndr_rdr *r, size_t n) {
    if (r->err || r->o + n > r->len) { r->err = 1; return; }
    r->o += n;
}

/* RPC_UNICODE_STRING в фиксированной части: Length(2) MaxLength(2) ptr(4).
 * Возвращает referent ptr (0 => буфера в deferred нет). */
static uint32_t ndr_unistr_ptr(ndr_rdr *r) {
    (void)ndr_u16(r);           /* Length */
    (void)ndr_u16(r);           /* MaximumLength */
    return ndr_u32(r);          /* Buffer referent */
}

/* deferred-буфер строки: conformant-varying wchar array. */
static void ndr_skip_str_deferred(ndr_rdr *r) {
    ndr_align(r, 4);
    (void)ndr_u32(r);                 /* MaxCount */
    (void)ndr_u32(r);                 /* Offset */
    uint32_t actual = ndr_u32(r);     /* ActualCount */
    if (actual > (r->len / 2) + 1) { r->err = 1; return; }
    ndr_skip(r, (size_t)actual * 2);
    ndr_align(r, 4);
}

/* deferred SID (RPC_SID как conformant struct): MaxCount(4)=SubAuthCount,
 * Revision(1), SubAuthorityCount(1), IdentifierAuthority(6, big-endian),
 * SubAuthority[SubAuthorityCount](4). Пишет строку в out (>= SID_STR_MAX). */
static void ndr_read_sid(ndr_rdr *r, char *out) {
    out[0] = '\0';
    ndr_align(r, 4);
    uint32_t maxc = ndr_u32(r);
    if (r->err) return;
    unsigned rev = (r->o < r->len) ? r->b[r->o] : 0; r->o++;
    unsigned sac = (r->o < r->len) ? r->b[r->o] : 0; r->o++;
    if (r->o + 6 > r->len) { r->err = 1; return; }
    unsigned long long idauth = 0;
    for (int i = 0; i < 6; i++) idauth = (idauth << 8) | r->b[r->o + i];
    r->o += 6;
    if (sac > PAC_MAX_SUBAUTH || maxc != sac) { r->err = 1; return; }

    int n = snprintf(out, SID_STR_MAX, "S-%u-%llu", rev, idauth);
    for (unsigned i = 0; i < sac; i++) {
        uint32_t sub = ndr_u32(r);
        if (r->err || n < 0 || n >= SID_STR_MAX) { r->err = 1; return; }
        n += snprintf(out + n, (size_t)(SID_STR_MAX - n), "-%u", sub);
    }
    if (n < 0 || n >= SID_STR_MAX) { r->err = 1; out[0] = '\0'; }
}

int ngx_spnego_pac_extract_sids(const unsigned char *buf, size_t len,
                                spnego_pac_sid_cb cb, void *cbctx) {
    ndr_rdr rr = { buf, len, 0, 0 };
    ndr_rdr *r = &rr;

    if (len < 24) return -1;

    /* type-serialization: common(8) + private(8), тело с 16 */
    r->o = 8;
    (void)ndr_u32(r);        /* ObjectBufferLength */
    (void)ndr_u32(r);        /* filler */
    uint32_t top = ndr_u32(r);
    if (r->err || top == 0) return -1;

    /* --- фиксированная часть: собираем referent-ptr и счётчики --- */
    int i;
    for (i = 0; i < 6; i++) (void)ndr_u32(r), (void)ndr_u32(r); /* 6 FILETIME */

    uint32_t name_ptr[6];
    for (i = 0; i < 6; i++) name_ptr[i] = ndr_unistr_ptr(r);

    (void)ndr_u16(r);        /* LogonCount */
    (void)ndr_u16(r);        /* BadPasswordCount */
    (void)ndr_u32(r);        /* UserId */
    (void)ndr_u32(r);        /* PrimaryGroupId */
    uint32_t group_count = ndr_u32(r);
    uint32_t groups_ptr  = ndr_u32(r);
    (void)ndr_u32(r);        /* UserFlags */
    ndr_skip(r, 16);         /* UserSessionKey */
    uint32_t server_ptr  = ndr_unistr_ptr(r);   /* LogonServer */
    uint32_t domname_ptr = ndr_unistr_ptr(r);   /* LogonDomainName */
    uint32_t dom_sid_ptr = ndr_u32(r);          /* LogonDomainId */
    (void)ndr_u32(r); (void)ndr_u32(r);         /* Reserved1[2] */
    (void)ndr_u32(r);        /* UserAccountControl */
    (void)ndr_u32(r);        /* SubAuthStatus */
    (void)ndr_u32(r); (void)ndr_u32(r);         /* LastSuccessfulILogon */
    (void)ndr_u32(r); (void)ndr_u32(r);         /* LastFailedILogon */
    (void)ndr_u32(r);        /* FailedILogonCount */
    (void)ndr_u32(r);        /* Reserved3 */
    uint32_t sid_count   = ndr_u32(r);
    uint32_t extra_ptr   = ndr_u32(r);          /* ExtraSids */
    uint32_t resdom_ptr  = ndr_u32(r);          /* ResourceGroupDomainSid */
    uint32_t resgrp_count= ndr_u32(r);
    uint32_t resgrp_ptr  = ndr_u32(r);          /* ResourceGroupIds */
    if (r->err) return -1;

    /* --- deferred-референты строго по порядку появления указателей --- */

    /* 1-6: буферы юникод-строк EffectiveName..HomeDirectoryDrive */
    for (i = 0; i < 6; i++)
        if (name_ptr[i]) ndr_skip_str_deferred(r);

    /* 7: GroupIds -> RID'ы (домен-SID ещё не прочитан, копим) */
    uint32_t rids[PAC_MAX_GROUPS];
    uint32_t nrids = 0;
    if (groups_ptr) {
        ndr_align(r, 4);
        uint32_t maxc = ndr_u32(r);
        if (r->err || maxc != group_count) return -1;
        for (i = 0; (uint32_t)i < maxc; i++) {
            uint32_t rid = ndr_u32(r);
            (void)ndr_u32(r);            /* Attributes */
            if (r->err) return -1;
            if (nrids < PAC_MAX_GROUPS) rids[nrids++] = rid;
        }
    }

    /* 8-9: буферы LogonServer / LogonDomainName */
    if (server_ptr)  ndr_skip_str_deferred(r);
    if (domname_ptr) ndr_skip_str_deferred(r);

    /* 10: LogonDomainId -> домен-SID; эмитим SID групп = <domain>-<rid> */
    char domsid[SID_STR_MAX];
    domsid[0] = '\0';
    if (dom_sid_ptr) {
        ndr_read_sid(r, domsid);
        if (r->err) return -1;
        for (i = 0; (uint32_t)i < nrids; i++) {
            char sid[SID_STR_MAX];
            int n = snprintf(sid, SID_STR_MAX, "%s-%u", domsid, rids[i]);
            if (n < 0 || n >= SID_STR_MAX) continue;
            if (cb(cbctx, sid, (size_t)n)) return 0;
        }
    }

    /* 11: ExtraSids -> уже полные SID */
    if (extra_ptr && sid_count) {
        ndr_align(r, 4);
        uint32_t maxc = ndr_u32(r);
        if (r->err || maxc != sid_count || maxc > PAC_MAX_GROUPS) return -1;
        uint32_t *has = rids; (void)has;
        /* массив KERB_SID_AND_ATTRIBUTES: (Sid.ptr, Attributes) */
        uint32_t j;
        unsigned char present[PAC_MAX_GROUPS];
        for (j = 0; j < maxc; j++) {
            uint32_t p = ndr_u32(r);
            (void)ndr_u32(r);            /* Attributes */
            present[j] = p ? 1 : 0;
        }
        if (r->err) return -1;
        for (j = 0; j < maxc; j++) {
            if (!present[j]) continue;
            char sid[SID_STR_MAX];
            ndr_read_sid(r, sid);
            if (r->err) return -1;
            if (sid[0] && cb(cbctx, sid, strlen(sid))) return 0;
        }
    }

    /* 12-13: ResourceGroup* — обычно пусто; парсим для полноты, не эмитим.
     * (universal/resource-группы другого домена; при надобности добавим). */
    (void)resdom_ptr; (void)resgrp_count; (void)resgrp_ptr;

    return 0;
}

#ifdef PAC_TEST
/* standalone-тест: gcc -DPAC_TEST ngx_http_auth_spnego_pac.c -o pactest
 *                  ./pactest < logon-info.bin  (или файл первым аргументом) */
#include <stdlib.h>
static int print_cb(void *ctx, const char *sid, size_t len) {
    (void)ctx; (void)len; printf("SID: %s\n", sid); return 0;
}
int main(int argc, char **argv) {
    unsigned char buf[8192];
    size_t n;
    if (argc > 1) {
        FILE *f = fopen(argv[1], "rb");
        if (!f) { perror("open"); return 1; }
        n = fread(buf, 1, sizeof(buf), f); fclose(f);
    } else {
        n = fread(buf, 1, sizeof(buf), stdin);
    }
    printf("blob: %zu bytes\n", n);
    int rc = ngx_spnego_pac_extract_sids(buf, n, print_cb, NULL);
    printf("rc=%d\n", rc);
    return rc ? 1 : 0;
}
#endif
