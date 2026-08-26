/* in_probe — чёрный ящик: проверяет доступность снаружи, как это делают
 * nmap, ping, openssl s_client и curl, и отдаёт результат метриками или
 * записями.
 *
 *   Type tcp    состояние порта: open / closed / filtered — по тому же
 *               признаку, что и у nmap (ответ, отказ, тишина)
 *   Type icmp   эхо-запрос; сокет берётся непривилегированный (ping-сокет),
 *               с откатом на raw, если ping_group_range закрыт
 *   Type tls    рукопожатие и разбор сертификата: срок, издатель, версия
 *               протокола, результат проверки цепочки. Умеет преамбулы:
 *               postgres (SSLRequest) и LDAP StartTLS — там TLS начинается
 *               не сразу, а после переговоров
 *   Type http   запрос произвольным методом, проверка кода и тела
 *
 * Имена метрик намеренно как у blackbox_exporter (probe_success,
 * probe_duration_seconds, probe_http_status_code, probe_ssl_earliest_cert_expiry
 * и т.д.): готовые дашборды и алерты работают без переписывания.
 *
 * Загрузчик ищет в .so структуру `in_probe_plugin`.
 */

#include <fluent-bit/flb_input_plugin.h>
#include <fluent-bit/flb_config_map.h>
#include <fluent-bit/flb_sds.h>
#include <fluent-bit/flb_time.h>
#include <fluent-bit/flb_log_event_encoder.h>
#include <fluent-bit/flb_http_client.h>
#include <fluent-bit/flb_upstream.h>

#include "dbmetrics.h"

#include <openssl/ssl.h>
#include <openssl/x509.h>
#include <openssl/err.h>

#include <netdb.h>
#include <netinet/in.h>
#include <netinet/ip_icmp.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <arpa/inet.h>
#include <poll.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

/* что проверяем */
#define PROBE_TCP   0
#define PROBE_ICMP  1
#define PROBE_TLS   2
#define PROBE_HTTP  3

/* состояние порта, как у nmap */
#define PORT_CLOSED    0
#define PORT_OPEN      1
#define PORT_FILTERED  2

/* с чего начинается TLS */
#define PREAMBLE_NONE      0
#define PREAMBLE_POSTGRES  1
#define PREAMBLE_LDAP      2

#define DEFAULT_TIMEOUT_MS 5000

struct probe_target {
    flb_sds_t raw;              /* как записано в конфигурации */
    flb_sds_t host;
    int port;
    flb_sds_t uri;              /* для http */
    int tls;                    /* для http: схема https */
    struct mk_list _head;
};

struct flb_in_probe {
    struct flb_input_instance *ins;
    struct mk_list targets;

    /* конфигурация */
    flb_sds_t targets_conf;
    flb_sds_t type_conf;
    int type;
    int timeout_ms;
    flb_sds_t mode_conf;
    int mode;
    flb_sds_t metrics_tag;
    flb_sds_t schedule_conf;
    struct dbm_schedule schedule;
    int interval_sec;
    int interval_nsec;

    /* tls */
    flb_sds_t preamble_conf;
    int preamble;
    int tls_verify;
    flb_sds_t tls_ca_file;
    flb_sds_t tls_servername;

    /* http */
    flb_sds_t method_conf;
    flb_sds_t body;
    flb_sds_t headers_conf;
    flb_sds_t expect_status;
    flb_sds_t expect_body;

    int coll_fd;
    struct flb_log_event_encoder log_encoder;
};

/* Итог одной пробы. Заполняется целиком, потом раскладывается в метрики
 * и/или в запись — чтобы обе ветки видели одно и то же. */
struct probe_result {
    int success;
    double duration_sec;
    const char *failure;        /* короткая причина, если success == 0 */

    /* tcp */
    int has_port_state;
    int port_state;

    /* tls */
    int has_tls;
    double cert_expiry_unix;
    double cert_expiry_days;
    int verify_result;
    char tls_version[32];
    char cert_subject[256];
    char cert_not_after[32];    /* дата истечения, ISO-8601 */

    /* http */
    int has_http;
    int status_code;
    long content_length;
    int body_matched;
};

/* ------------------------------------------------------------------ разбор */

static int probe_type_parse(const char *v)
{
    if (!v) {
        return -1;
    }
    if (strcasecmp(v, "tcp") == 0)  return PROBE_TCP;
    if (strcasecmp(v, "icmp") == 0) return PROBE_ICMP;
    if (strcasecmp(v, "tls") == 0)  return PROBE_TLS;
    if (strcasecmp(v, "http") == 0) return PROBE_HTTP;
    return -1;
}

static int preamble_parse(const char *v)
{
    if (!v || *v == '\0' || strcasecmp(v, "none") == 0) return PREAMBLE_NONE;
    if (strcasecmp(v, "postgres") == 0)  return PREAMBLE_POSTGRES;
    if (strcasecmp(v, "ldap") == 0 ||
        strcasecmp(v, "ldap-starttls") == 0) return PREAMBLE_LDAP;
    return -1;
}

/* "https://host:443/path" | "host:port" | "host" → цель */
static struct probe_target *target_create(struct flb_in_probe *ctx,
                                          const char *item)
{
    struct probe_target *t;
    const char *p = item;
    const char *slash;
    const char *colon;
    char hostbuf[256];
    size_t len;

    t = flb_calloc(1, sizeof(struct probe_target));
    if (!t) {
        flb_errno();
        return NULL;
    }
    t->raw = flb_sds_create(item);
    t->port = 0;

    if (strncasecmp(p, "https://", 8) == 0) {
        t->tls = FLB_TRUE;
        p += 8;
        t->port = 443;
    }
    else if (strncasecmp(p, "http://", 7) == 0) {
        p += 7;
        t->port = 80;
    }

    slash = strchr(p, '/');
    if (slash) {
        t->uri = flb_sds_create(slash);
        len = (size_t) (slash - p);
    }
    else {
        t->uri = flb_sds_create("/");
        len = strlen(p);
    }
    if (len >= sizeof(hostbuf)) {
        len = sizeof(hostbuf) - 1;
    }
    memcpy(hostbuf, p, len);
    hostbuf[len] = '\0';

    colon = strrchr(hostbuf, ':');
    if (colon) {
        t->port = atoi(colon + 1);
        *((char *) colon) = '\0';
    }
    t->host = flb_sds_create(hostbuf);

    if (t->port <= 0 && ctx->type != PROBE_ICMP) {
        flb_plg_error(ctx->ins, "в цели «%s» не указан порт", item);
        flb_sds_destroy(t->raw);
        flb_sds_destroy(t->host);
        flb_sds_destroy(t->uri);
        flb_free(t);
        return NULL;
    }
    return t;
}

static int parse_targets(struct flb_in_probe *ctx)
{
    char *copy;
    char *save = NULL;
    char *item;
    struct probe_target *t;

    copy = flb_strdup(ctx->targets_conf);
    if (!copy) {
        return -1;
    }
    item = strtok_r(copy, ", \t", &save);
    while (item) {
        t = target_create(ctx, item);
        if (!t) {
            flb_free(copy);
            return -1;
        }
        mk_list_add(&t->_head, &ctx->targets);
        item = strtok_r(NULL, ", \t", &save);
    }
    flb_free(copy);

    if (mk_list_size(&ctx->targets) == 0) {
        flb_plg_error(ctx->ins, "в targets нет ни одной цели");
        return -1;
    }
    return 0;
}

/* --------------------------------------------------------------- сокеты */

static double elapsed_sec(struct flb_time *t0)
{
    struct flb_time t1;

    flb_time_get(&t1);
    return (double) (flb_time_to_nanosec(&t1) - flb_time_to_nanosec(t0)) / 1e9;
}

/* Адрес цели. Разрешение имени — часть пробы: не разрешилось, значит недоступно. */
static struct addrinfo *resolve(struct flb_in_probe *ctx, struct probe_target *t,
                                int socktype)
{
    struct addrinfo hints;
    struct addrinfo *res = NULL;
    char port[16];
    int ret;

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;          /* ipv6 — отдельная история, см. README */
    hints.ai_socktype = socktype;

    snprintf(port, sizeof(port), "%i", t->port > 0 ? t->port : 0);
    ret = getaddrinfo(t->host, t->port > 0 ? port : NULL, &hints, &res);
    if (ret != 0) {
        flb_plg_debug(ctx->ins, "%s: имя не разрешилось: %s",
                      t->host, gai_strerror(ret));
        return NULL;
    }
    return res;
}

/* Неблокирующий connect с таймаутом. Возвращает PORT_*; при open отдаёт
 * открытый сокет через out_fd (иначе закрывает сам).
 *
 * Так же различает состояния nmap: ответили — open, отказали (RST) — closed,
 * промолчали — filtered. */
static int tcp_connect(struct flb_in_probe *ctx, struct probe_target *t,
                       int *out_fd)
{
    struct addrinfo *ai;
    struct pollfd pfd;
    int fd;
    int flags;
    int ret;
    int err = 0;
    socklen_t len = sizeof(err);
    int state = PORT_FILTERED;

    if (out_fd) {
        *out_fd = -1;
    }

    ai = resolve(ctx, t, SOCK_STREAM);
    if (!ai) {
        return PORT_FILTERED;
    }

    fd = socket(ai->ai_family, SOCK_STREAM, 0);
    if (fd < 0) {
        freeaddrinfo(ai);
        return PORT_FILTERED;
    }

    flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);

    ret = connect(fd, ai->ai_addr, ai->ai_addrlen);
    if (ret == 0) {
        state = PORT_OPEN;
        goto done;
    }
    if (errno != EINPROGRESS) {
        state = (errno == ECONNREFUSED) ? PORT_CLOSED : PORT_FILTERED;
        goto done;
    }

    pfd.fd = fd;
    pfd.events = POLLOUT;
    ret = poll(&pfd, 1, ctx->timeout_ms);
    if (ret == 0) {
        /* тишина до таймаута — пакет съел фильтр */
        state = PORT_FILTERED;
        goto done;
    }
    if (ret < 0) {
        state = PORT_FILTERED;
        goto done;
    }

    if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &len) != 0) {
        state = PORT_FILTERED;
        goto done;
    }
    if (err == 0) {
        state = PORT_OPEN;
    }
    else if (err == ECONNREFUSED) {
        state = PORT_CLOSED;
    }
    else {
        /* EHOSTUNREACH/ENETUNREACH — до порта не дошли, это тоже фильтр */
        state = PORT_FILTERED;
    }

done:
    freeaddrinfo(ai);
    if (state == PORT_OPEN && out_fd) {
        fcntl(fd, F_SETFL, flags);      /* дальше работаем блокирующе */
        *out_fd = fd;
    }
    else {
        close(fd);
    }
    return state;
}

/* ------------------------------------------------------------------- ICMP */

static uint16_t icmp_checksum(void *data, int len)
{
    uint16_t *w = data;
    uint32_t sum = 0;

    while (len > 1) {
        sum += *w++;
        len -= 2;
    }
    if (len == 1) {
        sum += *(uint8_t *) w;
    }
    sum = (sum >> 16) + (sum & 0xffff);
    sum += (sum >> 16);
    return (uint16_t) ~sum;
}

/* Эхо-запрос. Сначала пробуем непривилегированный ping-сокет (он разрешён,
 * если net.ipv4.ping_group_range накрывает нашу группу), при отказе —
 * raw-сокет, для которого нужен CAP_NET_RAW. */
static int icmp_probe(struct flb_in_probe *ctx, struct probe_target *t,
                      struct probe_result *r)
{
    struct addrinfo *ai;
    struct icmphdr req;
    char buf[1500];
    struct pollfd pfd;
    int fd;
    int raw = FLB_FALSE;
    ssize_t n;
    uint16_t seq = (uint16_t) (time(NULL) & 0xffff);

    ai = resolve(ctx, t, SOCK_DGRAM);
    if (!ai) {
        r->failure = "имя не разрешилось";
        return -1;
    }

    fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_ICMP);
    if (fd < 0) {
        fd = socket(AF_INET, SOCK_RAW, IPPROTO_ICMP);
        raw = FLB_TRUE;
    }
    if (fd < 0) {
        freeaddrinfo(ai);
        r->failure = "нет прав на icmp: нужен CAP_NET_RAW или ping_group_range";
        return -1;
    }

    memset(&req, 0, sizeof(req));
    req.type = ICMP_ECHO;
    req.code = 0;
    req.un.echo.id = (uint16_t) (getpid() & 0xffff);
    req.un.echo.sequence = seq;
    req.checksum = 0;
    req.checksum = icmp_checksum(&req, sizeof(req));

    if (sendto(fd, &req, sizeof(req), 0, ai->ai_addr, ai->ai_addrlen) < 0) {
        close(fd);
        freeaddrinfo(ai);
        r->failure = "icmp не отправился";
        return -1;
    }
    freeaddrinfo(ai);

    /* ждём эхо-ответ; чужие пакеты на raw-сокете пропускаем */
    while (1) {
        pfd.fd = fd;
        pfd.events = POLLIN;
        if (poll(&pfd, 1, ctx->timeout_ms) <= 0) {
            close(fd);
            r->failure = "нет ответа";
            return -1;
        }
        n = recv(fd, buf, sizeof(buf), 0);
        if (n <= 0) {
            close(fd);
            r->failure = "нет ответа";
            return -1;
        }

        {
            struct icmphdr *rep;
            size_t off = 0;

            if (raw) {
                struct iphdr *ip = (struct iphdr *) buf;
                off = (size_t) ip->ihl * 4;
            }
            if ((size_t) n < off + sizeof(struct icmphdr)) {
                continue;
            }
            rep = (struct icmphdr *) (buf + off);
            if (rep->type != ICMP_ECHOREPLY) {
                continue;
            }
            /* у ping-сокета ядро подменяет id, поэтому сверяем номер */
            if (rep->un.echo.sequence != seq) {
                continue;
            }
            break;
        }
    }

    close(fd);
    return 0;
}

/* -------------------------------------------------------------------- TLS */

/* Преамбула PostgreSQL: 8 байт SSLRequest, в ответ один байт S (можно) или
 * N (сервер без TLS). Без неё «просто TLS на 5432» не работает. */
static int preamble_postgres(int fd)
{
    unsigned char req[8] = {0x00, 0x00, 0x00, 0x08, 0x04, 0xd2, 0x16, 0x2f};
    char reply = 0;

    if (write(fd, req, sizeof(req)) != (ssize_t) sizeof(req)) {
        return -1;
    }
    if (read(fd, &reply, 1) != 1) {
        return -1;
    }
    return reply == 'S' ? 0 : -1;
}

/* Преамбула LDAP StartTLS: extendedReq с OID 1.3.6.1.4.1.1466.20037.
 * Ответ разбираем грубо — нам нужен только resultCode 0 в начале LDAPMessage. */
static int preamble_ldap(int fd)
{
    static const unsigned char req[] = {
        0x30, 0x1d,                                     /* LDAPMessage */
        0x02, 0x01, 0x01,                               /* messageID 1 */
        0x77, 0x18,                                     /* extendedReq */
        0x80, 0x16,                                     /* requestName */
        '1','.','3','.','6','.','1','.','4','.','1','.',
        '1','4','6','6','.','2','0','0','3','7'
    };
    unsigned char buf[256];
    ssize_t n;
    ssize_t i;

    if (write(fd, req, sizeof(req)) != (ssize_t) sizeof(req)) {
        return -1;
    }
    n = read(fd, buf, sizeof(buf));
    if (n < 10) {
        return -1;
    }
    /* ищем ENUMERATED resultCode (0x0a 0x01 0xXX) — 0 значит «можно» */
    for (i = 0; i + 2 < n; i++) {
        if (buf[i] == 0x0a && buf[i + 1] == 0x01) {
            return buf[i + 2] == 0 ? 0 : -1;
        }
    }
    return -1;
}

static int tls_probe(struct flb_in_probe *ctx, struct probe_target *t,
                     struct probe_result *r)
{
    SSL_CTX *sctx = NULL;
    SSL *ssl = NULL;
    X509 *cert = NULL;
    ASN1_TIME *not_after;
    ASN1_TIME *now_asn1 = NULL;
    const char *ver;
    char *subject;
    int days = 0;
    int secs = 0;
    int fd = -1;
    int rc = -1;

    if (tcp_connect(ctx, t, &fd) != PORT_OPEN) {
        r->failure = "порт не открылся";
        return -1;
    }

    /* дальше работаем блокирующе (преамбула и рукопожатие), поэтому таймаут
     * обязателен: порт, который не понимает преамбулу, просто молчит, и без
     * него проба зависла бы навсегда вместе со своим потоком */
    {
        struct timeval tv;

        tv.tv_sec = ctx->timeout_ms / 1000;
        tv.tv_usec = (ctx->timeout_ms % 1000) * 1000;
        setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
    }

    switch (ctx->preamble) {
    case PREAMBLE_POSTGRES:
        if (preamble_postgres(fd) != 0) {
            r->failure = "postgres отказал в TLS (ответ не S)";
            goto done;
        }
        break;
    case PREAMBLE_LDAP:
        if (preamble_ldap(fd) != 0) {
            r->failure = "ldap отказал в StartTLS";
            goto done;
        }
        break;
    default:
        break;
    }

    sctx = SSL_CTX_new(TLS_client_method());
    if (!sctx) {
        r->failure = "нет контекста TLS";
        goto done;
    }
    if (ctx->tls_ca_file && flb_sds_len(ctx->tls_ca_file) > 0) {
        SSL_CTX_load_verify_locations(sctx, ctx->tls_ca_file, NULL);
    }
    else {
        SSL_CTX_set_default_verify_paths(sctx);
    }

    ssl = SSL_new(sctx);
    if (!ssl) {
        r->failure = "нет сессии TLS";
        goto done;
    }
    SSL_set_fd(ssl, fd);

    /* SNI: без него сервер с несколькими именами отдаст не тот сертификат */
    {
        const char *sni = (ctx->tls_servername &&
                           flb_sds_len(ctx->tls_servername) > 0)
                          ? ctx->tls_servername : t->host;
        SSL_set_tlsext_host_name(ssl, sni);
        if (ctx->tls_verify) {
            SSL_set1_host(ssl, sni);
        }
    }

    if (SSL_connect(ssl) != 1) {
        r->failure = "рукопожатие не прошло";
        goto done;
    }

    r->has_tls = FLB_TRUE;
    ver = SSL_get_version(ssl);
    snprintf(r->tls_version, sizeof(r->tls_version), "%s", ver ? ver : "");
    r->verify_result = (int) SSL_get_verify_result(ssl);

    cert = SSL_get1_peer_certificate(ssl);
    if (!cert) {
        r->failure = "сервер не прислал сертификат";
        goto done;
    }

    subject = X509_NAME_oneline(X509_get_subject_name(cert), NULL, 0);
    if (subject) {
        snprintf(r->cert_subject, sizeof(r->cert_subject), "%s", subject);
        OPENSSL_free(subject);
    }

    not_after = X509_getm_notAfter(cert);
    if (not_after && ASN1_TIME_diff(&days, &secs, NULL, not_after)) {
        /* ASN1_TIME_diff даёт разницу с текущим моментом; абсолютное время
         * получается прибавлением к нему — точно до секунды */
        r->cert_expiry_days = (double) days + (double) secs / 86400.0;
        r->cert_expiry_unix = (double) time(NULL) + (double) days * 86400.0 +
                              (double) secs;
        {
            time_t when = (time_t) r->cert_expiry_unix;
            struct tm gm;

            gmtime_r(&when, &gm);
            strftime(r->cert_not_after, sizeof(r->cert_not_after),
                     "%Y-%m-%dT%H:%M:%SZ", &gm);
        }
    }

    /* проверка цепочки — отдельно от «соединение установилось»: сертификат
     * может быть просрочен или самоподписан, а рукопожатие пройти */
    if (ctx->tls_verify && r->verify_result != X509_V_OK) {
        r->failure = "сертификат не прошёл проверку";
        goto done;
    }

    rc = 0;

done:
    if (now_asn1) {
        ASN1_STRING_free(now_asn1);
    }
    if (cert) {
        X509_free(cert);
    }
    if (ssl) {
        SSL_shutdown(ssl);
        SSL_free(ssl);
    }
    if (sctx) {
        SSL_CTX_free(sctx);
    }
    if (fd >= 0) {
        close(fd);
    }
    return rc;
}

/* ------------------------------------------------------------------- HTTP */

static int http_method_id(const char *m)
{
    if (!m || strcasecmp(m, "GET") == 0)  return FLB_HTTP_GET;
    if (strcasecmp(m, "POST") == 0)       return FLB_HTTP_POST;
    if (strcasecmp(m, "PUT") == 0)        return FLB_HTTP_PUT;
    if (strcasecmp(m, "HEAD") == 0)       return FLB_HTTP_HEAD;
    return -1;
}

/* "200" | "200,204" | "2xx" — ожидаемый код */
static int status_expected(const char *spec, int code)
{
    char buf[128];
    char *save = NULL;
    char *item;
    int ok = FLB_FALSE;

    if (!spec || *spec == '\0') {
        return (code >= 200 && code < 400) ? FLB_TRUE : FLB_FALSE;
    }
    snprintf(buf, sizeof(buf), "%s", spec);
    item = strtok_r(buf, ", \t", &save);
    while (item && !ok) {
        if (strlen(item) == 3 && (item[1] == 'x' || item[1] == 'X')) {
            ok = (code / 100) == (item[0] - '0');
        }
        else if (atoi(item) == code) {
            ok = FLB_TRUE;
        }
        item = strtok_r(NULL, ", \t", &save);
    }
    return ok;
}

static void http_add_headers(struct flb_http_client *c, const char *spec)
{
    char buf[1024];
    char *save = NULL;
    char *item;
    char *colon;

    if (!spec || *spec == '\0') {
        return;
    }
    snprintf(buf, sizeof(buf), "%s", spec);
    /* "K: V; K2: V2" — точка с запятой, чтобы запятая осталась значению */
    item = strtok_r(buf, ";", &save);
    while (item) {
        while (*item == ' ') {
            item++;
        }
        colon = strchr(item, ':');
        if (colon) {
            *colon = '\0';
            colon++;
            while (*colon == ' ') {
                colon++;
            }
            flb_http_add_header(c, item, strlen(item), colon, strlen(colon));
        }
        item = strtok_r(NULL, ";", &save);
    }
}

static int http_probe(struct flb_in_probe *ctx, struct probe_target *t,
                      struct probe_result *r, struct flb_config *config)
{
    struct flb_upstream *u;
    struct flb_connection *conn;
    struct flb_http_client *c;
    size_t b_sent;
    int io_flags;
    int method;
    int rc = -1;

    method = http_method_id(ctx->method_conf);
    if (method < 0) {
        r->failure = "неизвестный метод";
        return -1;
    }

    io_flags = t->tls ? (FLB_IO_TCP | FLB_IO_TLS) : FLB_IO_TCP;
    u = flb_upstream_create(config, t->host, t->port, io_flags,
                            t->tls ? ctx->ins->tls : NULL);
    if (!u) {
        r->failure = "нет соединения";
        return -1;
    }
    flb_stream_disable_async_mode(&u->base);

    conn = flb_upstream_conn_get(u);
    if (!conn) {
        r->failure = "не подключиться";
        goto out;
    }

    c = flb_http_client(conn, method, t->uri,
                        ctx->body && flb_sds_len(ctx->body) > 0 ? ctx->body : NULL,
                        ctx->body ? flb_sds_len(ctx->body) : 0,
                        t->host, t->port, NULL, 0);
    if (!c) {
        flb_upstream_conn_release(conn);
        r->failure = "запрос не собрался";
        goto out;
    }
    flb_http_buffer_size(c, 0);
    http_add_headers(c, ctx->headers_conf);

    if (flb_http_do(c, &b_sent) != 0) {
        flb_http_client_destroy(c);
        flb_upstream_conn_release(conn);
        r->failure = "запрос не прошёл";
        goto out;
    }

    r->has_http = FLB_TRUE;
    r->status_code = c->resp.status;
    r->content_length = (long) c->resp.payload_size;
    r->body_matched = FLB_TRUE;

    if (ctx->expect_body && flb_sds_len(ctx->expect_body) > 0) {
        r->body_matched = (c->resp.payload &&
                           memmem(c->resp.payload, c->resp.payload_size,
                                  ctx->expect_body,
                                  flb_sds_len(ctx->expect_body)) != NULL)
                          ? FLB_TRUE : FLB_FALSE;
    }

    if (!status_expected(ctx->expect_status, r->status_code)) {
        r->failure = "неожиданный код ответа";
    }
    else if (!r->body_matched) {
        r->failure = "в теле нет ожидаемого текста";
    }
    else {
        rc = 0;
    }

    flb_http_client_destroy(c);
    flb_upstream_conn_release(conn);

out:
    flb_upstream_destroy(u);
    return rc;
}

/* --------------------------------------------------------------- один прогон */

static void probe_run(struct flb_in_probe *ctx, struct probe_target *t,
                      struct probe_result *r, struct flb_config *config)
{
    struct flb_time t0;
    int state;

    memset(r, 0, sizeof(*r));
    flb_time_get(&t0);

    switch (ctx->type) {
    case PROBE_TCP:
        state = tcp_connect(ctx, t, NULL);
        r->has_port_state = FLB_TRUE;
        r->port_state = state;
        r->success = (state == PORT_OPEN);
        if (!r->success) {
            r->failure = (state == PORT_CLOSED) ? "порт закрыт" : "порт отфильтрован";
        }
        break;

    case PROBE_ICMP:
        r->success = (icmp_probe(ctx, t, r) == 0);
        break;

    case PROBE_TLS:
        r->success = (tls_probe(ctx, t, r) == 0);
        break;

    case PROBE_HTTP:
        r->success = (http_probe(ctx, t, r, config) == 0);
        break;
    }

    /* время меряем здесь, а не внутри каждой пробы: иначе на неудачных ветках
     * его легко забыть, и в метрику уходит ноль вместо реального таймаута */
    r->duration_sec = elapsed_sec(&t0);
}

/* ------------------------------------------------------- вывод: метрики */

static const char *port_state_name(int state)
{
    switch (state) {
    case PORT_OPEN:     return "open";
    case PORT_CLOSED:   return "closed";
    default:            return "filtered";
    }
}

static void emit_metrics(struct flb_in_probe *ctx, struct probe_target *t,
                         struct probe_result *r, struct dbm_set *set,
                         uint64_t ts)
{
    dbm_set_row_begin(set, t->raw);
    dbm_set_label(set, 0, ctx->type_conf, strlen(ctx->type_conf));

    dbm_set_value(set, ctx->ins, "probe", "success", 7, r->success ? 1 : 0, ts);
    dbm_set_value(set, ctx->ins, "probe", "duration_seconds", 16,
                  r->duration_sec, ts);

    if (r->has_port_state) {
        /* своё поверх blackbox: 1 open, 0 closed, 2 filtered */
        dbm_set_value(set, ctx->ins, "probe", "port_state", 10,
                      r->port_state, ts);
    }
    if (ctx->type == PROBE_ICMP) {
        dbm_set_value(set, ctx->ins, "probe", "icmp_duration_seconds", 21,
                      r->duration_sec, ts);
    }
    if (r->has_tls) {
        dbm_set_value(set, ctx->ins, "probe", "ssl_earliest_cert_expiry", 24,
                      r->cert_expiry_unix, ts);
        dbm_set_value(set, ctx->ins, "probe", "ssl_cert_expiry_days", 20,
                      r->cert_expiry_days, ts);
        dbm_set_value(set, ctx->ins, "probe", "ssl_verify_result", 17,
                      r->verify_result, ts);
    }
    if (r->has_http) {
        dbm_set_value(set, ctx->ins, "probe", "http_status_code", 16,
                      r->status_code, ts);
        dbm_set_value(set, ctx->ins, "probe", "http_content_length", 19,
                      r->content_length, ts);
        dbm_set_value(set, ctx->ins, "probe", "failed_due_to_regex", 19,
                      r->body_matched ? 0 : 1, ts);
    }
}

/* ------------------------------------------------------- вывод: записи */

static int emit_record(struct flb_in_probe *ctx, struct probe_target *t,
                       struct probe_result *r)
{
    struct flb_time tm;
    int ret;

    flb_time_get(&tm);
    ret = flb_log_event_encoder_begin_record(&ctx->log_encoder);
    if (ret == FLB_EVENT_ENCODER_SUCCESS) {
        ret = flb_log_event_encoder_set_timestamp(&ctx->log_encoder, &tm);
    }
    if (ret == FLB_EVENT_ENCODER_SUCCESS) {
        ret = flb_log_event_encoder_append_body_values(
            &ctx->log_encoder,
            FLB_LOG_EVENT_CSTRING_VALUE("instance"),
            FLB_LOG_EVENT_CSTRING_VALUE(t->raw),
            FLB_LOG_EVENT_CSTRING_VALUE("type"),
            FLB_LOG_EVENT_CSTRING_VALUE(ctx->type_conf),
            FLB_LOG_EVENT_CSTRING_VALUE("success"),
            FLB_LOG_EVENT_INT64_VALUE(r->success ? 1 : 0),
            FLB_LOG_EVENT_CSTRING_VALUE("duration_seconds"),
            FLB_LOG_EVENT_DOUBLE_VALUE(r->duration_sec),
            FLB_LOG_EVENT_CSTRING_VALUE("failure"),
            FLB_LOG_EVENT_CSTRING_VALUE(r->failure ? r->failure : ""));
    }
    if (ret == FLB_EVENT_ENCODER_SUCCESS && r->has_port_state) {
        ret = flb_log_event_encoder_append_body_values(
            &ctx->log_encoder,
            FLB_LOG_EVENT_CSTRING_VALUE("port_state"),
            FLB_LOG_EVENT_CSTRING_VALUE(port_state_name(r->port_state)));
    }
    if (ret == FLB_EVENT_ENCODER_SUCCESS && r->has_tls) {
        ret = flb_log_event_encoder_append_body_values(
            &ctx->log_encoder,
            FLB_LOG_EVENT_CSTRING_VALUE("tls_version"),
            FLB_LOG_EVENT_CSTRING_VALUE(r->tls_version),
            FLB_LOG_EVENT_CSTRING_VALUE("cert_subject"),
            FLB_LOG_EVENT_CSTRING_VALUE(r->cert_subject),
            FLB_LOG_EVENT_CSTRING_VALUE("cert_not_after"),
            FLB_LOG_EVENT_CSTRING_VALUE(r->cert_not_after),
            FLB_LOG_EVENT_CSTRING_VALUE("cert_expiry_days"),
            FLB_LOG_EVENT_DOUBLE_VALUE(r->cert_expiry_days),
            FLB_LOG_EVENT_CSTRING_VALUE("verify_result"),
            FLB_LOG_EVENT_INT64_VALUE(r->verify_result));
    }
    if (ret == FLB_EVENT_ENCODER_SUCCESS && r->has_http) {
        ret = flb_log_event_encoder_append_body_values(
            &ctx->log_encoder,
            FLB_LOG_EVENT_CSTRING_VALUE("status_code"),
            FLB_LOG_EVENT_INT64_VALUE(r->status_code),
            FLB_LOG_EVENT_CSTRING_VALUE("content_length"),
            FLB_LOG_EVENT_INT64_VALUE(r->content_length));
    }
    if (ret == FLB_EVENT_ENCODER_SUCCESS) {
        ret = flb_log_event_encoder_commit_record(&ctx->log_encoder);
    }
    if (ret != FLB_EVENT_ENCODER_SUCCESS) {
        flb_log_event_encoder_rollback_record(&ctx->log_encoder);
        return -1;
    }
    return 0;
}

/* ------------------------------------------------------------------- сбор */

static int cb_collect(struct flb_input_instance *ins,
                      struct flb_config *config, void *in_context)
{
    struct flb_in_probe *ctx = in_context;
    struct mk_list *head;
    struct probe_target *t;
    struct probe_result r;
    struct dbm_set set;
    struct dbm_set *set_ptr = NULL;
    struct flb_time tm;
    uint64_t ts;
    const char *tag = NULL;
    size_t tag_len = 0;
    int records = 0;

    (void) ins;

    if (!dbm_schedule_due(&ctx->schedule, time(NULL))) {
        return 0;
    }

    if (ctx->mode != DBM_MODE_LOGS) {
        struct dbm_names labels;
        static char *label_type = "type";

        memset(&labels, 0, sizeof(labels));
        labels.count = 1;
        labels.item = &label_type;
        if (dbm_set_init(&set, "instance", &labels) != 0) {
            flb_plg_error(ctx->ins, "набор метрик не создан");
            return -1;
        }
        set_ptr = &set;
    }

    flb_time_get(&tm);
    ts = flb_time_to_nanosec(&tm);

    mk_list_foreach(head, &ctx->targets) {
        t = mk_list_entry(head, struct probe_target, _head);
        probe_run(ctx, t, &r, config);

        if (set_ptr) {
            emit_metrics(ctx, t, &r, set_ptr, ts);
        }
        if (ctx->mode != DBM_MODE_METRICS) {
            if (emit_record(ctx, t, &r) == 0) {
                records++;
            }
        }
        if (!r.success) {
            flb_plg_debug(ctx->ins, "%s (%s): %s", t->raw, ctx->type_conf,
                          r.failure ? r.failure : "неудача");
        }
    }

    if (records > 0) {
        flb_input_log_append(ctx->ins, NULL, 0,
                             ctx->log_encoder.output_buffer,
                             ctx->log_encoder.output_length);
        flb_log_event_encoder_reset(&ctx->log_encoder);
    }
    if (set_ptr) {
        if (ctx->metrics_tag && flb_sds_len(ctx->metrics_tag) > 0) {
            tag = ctx->metrics_tag;
            tag_len = flb_sds_len(ctx->metrics_tag);
        }
        flb_input_metrics_append(ctx->ins, tag, tag_len, set.cmt);
        dbm_set_destroy(&set);
    }
    return 0;
}

/* -------------------------------------------------------------- жизненный цикл */

static int cb_init(struct flb_input_instance *ins,
                   struct flb_config *config, void *data)
{
    struct flb_in_probe *ctx;
    int ret;

    (void) data;

    ctx = flb_calloc(1, sizeof(struct flb_in_probe));
    if (!ctx) {
        flb_errno();
        return -1;
    }
    ctx->ins = ins;
    ctx->coll_fd = -1;
    mk_list_init(&ctx->targets);
    flb_input_set_context(ins, ctx);

    ret = flb_input_config_map_set(ins, (void *) ctx);
    if (ret == -1) {
        return -1;
    }

    ctx->type = probe_type_parse(ctx->type_conf);
    if (ctx->type < 0) {
        flb_plg_error(ins, "type: tcp, icmp, tls или http (задано %s)",
                      ctx->type_conf);
        return -1;
    }
    ctx->mode = dbm_mode_parse(ctx->mode_conf);
    if (ctx->mode < 0) {
        flb_plg_error(ins, "mode: logs, metrics или both (задано %s)",
                      ctx->mode_conf);
        return -1;
    }
    ctx->preamble = preamble_parse(ctx->preamble_conf);
    if (ctx->preamble < 0) {
        flb_plg_error(ins, "preamble: none, postgres или ldap-starttls (задано %s)",
                      ctx->preamble_conf);
        return -1;
    }
    if (ctx->type == PROBE_HTTP && http_method_id(ctx->method_conf) < 0) {
        flb_plg_error(ins, "method: GET, POST, PUT или HEAD (задано %s)",
                      ctx->method_conf);
        return -1;
    }
    if (!ctx->targets_conf || flb_sds_len(ctx->targets_conf) == 0) {
        flb_plg_error(ins, "нужен параметр targets");
        return -1;
    }
    if (parse_targets(ctx) != 0) {
        return -1;
    }
    if (dbm_schedule_init(&ctx->schedule, ctx->schedule_conf, ins,
                          ctx->interval_sec) != 0) {
        return -1;
    }

    ret = flb_log_event_encoder_init(&ctx->log_encoder,
                                     FLB_LOG_EVENT_FORMAT_DEFAULT);
    if (ret != FLB_EVENT_ENCODER_SUCCESS) {
        flb_plg_error(ins, "кодировщик записей не поднялся: %i", ret);
        return -1;
    }

    ret = flb_input_set_collector_time(ins, cb_collect,
                                       ctx->interval_sec, ctx->interval_nsec,
                                       config);
    if (ret == -1) {
        flb_plg_error(ins, "таймер сбора не заведён");
        return -1;
    }
    ctx->coll_fd = ret;

    flb_plg_info(ins, "проба %s, целей %i, таймаут %i мс, каждые %i с",
                 ctx->type_conf, mk_list_size(&ctx->targets),
                 ctx->timeout_ms, ctx->interval_sec);
    dbm_schedule_log(&ctx->schedule, ins, ctx->schedule_conf);
    return 0;
}

static void cb_pause(void *data, struct flb_config *config)
{
    struct flb_in_probe *ctx = data;
    (void) config;
    if (ctx->coll_fd >= 0) {
        flb_input_collector_pause(ctx->coll_fd, ctx->ins);
    }
}

static void cb_resume(void *data, struct flb_config *config)
{
    struct flb_in_probe *ctx = data;
    (void) config;
    if (ctx->coll_fd >= 0) {
        flb_input_collector_resume(ctx->coll_fd, ctx->ins);
    }
}

static int cb_exit(void *data, struct flb_config *config)
{
    struct flb_in_probe *ctx = data;
    struct mk_list *head;
    struct mk_list *tmp;
    struct probe_target *t;

    (void) config;

    if (!ctx) {
        return 0;
    }
    mk_list_foreach_safe(head, tmp, &ctx->targets) {
        t = mk_list_entry(head, struct probe_target, _head);
        mk_list_del(&t->_head);
        flb_sds_destroy(t->raw);
        flb_sds_destroy(t->host);
        flb_sds_destroy(t->uri);
        flb_free(t);
    }
    if (ctx->coll_fd >= 0) {
        flb_log_event_encoder_destroy(&ctx->log_encoder);
    }
    flb_free(ctx);
    return 0;
}

static struct flb_config_map config_map[] = {
    {
     FLB_CONFIG_MAP_STR, "type", "tcp",
     0, FLB_TRUE, offsetof(struct flb_in_probe, type_conf),
     "Что проверять: tcp, icmp, tls или http"
    },
    {
     FLB_CONFIG_MAP_STR, "targets", NULL,
     0, FLB_TRUE, offsetof(struct flb_in_probe, targets_conf),
     "Цели через запятую: host:port, host (для icmp) или url (для http)"
    },
    {
     FLB_CONFIG_MAP_INT, "timeout_ms", "5000",
     0, FLB_TRUE, offsetof(struct flb_in_probe, timeout_ms),
     "Сколько ждать ответа; по нему же порт признаётся отфильтрованным"
    },
    {
     FLB_CONFIG_MAP_STR, "mode", "metrics",
     0, FLB_TRUE, offsetof(struct flb_in_probe, mode_conf),
     "Что отдавать: metrics, logs или both"
    },
    {
     FLB_CONFIG_MAP_STR, "metrics_tag", NULL,
     0, FLB_TRUE, offsetof(struct flb_in_probe, metrics_tag),
     "Тег метрик; нужен в режиме both"
    },
    {
     FLB_CONFIG_MAP_STR, "preamble", "none",
     0, FLB_TRUE, offsetof(struct flb_in_probe, preamble_conf),
     "Переговоры до TLS: none, postgres (SSLRequest) или ldap-starttls"
    },
    {
     FLB_CONFIG_MAP_BOOL, "verify", "on",
     0, FLB_TRUE, offsetof(struct flb_in_probe, tls_verify),
     "Считать неудачей непроверенный сертификат"
    },
    {
     FLB_CONFIG_MAP_STR, "ca_file", NULL,
     0, FLB_TRUE, offsetof(struct flb_in_probe, tls_ca_file),
     "Корневой сертификат для проверки цепочки"
    },
    {
     FLB_CONFIG_MAP_STR, "servername", NULL,
     0, FLB_TRUE, offsetof(struct flb_in_probe, tls_servername),
     "Имя для SNI, если отличается от адреса цели"
    },
    {
     FLB_CONFIG_MAP_STR, "method", "GET",
     0, FLB_TRUE, offsetof(struct flb_in_probe, method_conf),
     "Метод HTTP: GET, POST, PUT, HEAD"
    },
    {
     FLB_CONFIG_MAP_STR, "body", NULL,
     0, FLB_TRUE, offsetof(struct flb_in_probe, body),
     "Тело запроса"
    },
    {
     FLB_CONFIG_MAP_STR, "headers", NULL,
     0, FLB_TRUE, offsetof(struct flb_in_probe, headers_conf),
     "Заголовки: «Ключ: значение; Ключ2: значение2»"
    },
    {
     FLB_CONFIG_MAP_STR, "expect_status", NULL,
     0, FLB_TRUE, offsetof(struct flb_in_probe, expect_status),
     "Ожидаемый код: 200, список 200,204 или класс 2xx; пусто — любой 2xx/3xx"
    },
    {
     FLB_CONFIG_MAP_STR, "expect_body", NULL,
     0, FLB_TRUE, offsetof(struct flb_in_probe, expect_body),
     "Подстрока, которая должна быть в теле ответа"
    },
    {
     FLB_CONFIG_MAP_STR, "schedule", NULL,
     0, FLB_TRUE, offsetof(struct flb_in_probe, schedule_conf),
     "cron-выражение: проба только в совпавшие моменты"
    },
    {
     FLB_CONFIG_MAP_INT, "interval_sec", "30",
     0, FLB_TRUE, offsetof(struct flb_in_probe, interval_sec),
     "Период проб, секунды"
    },
    {
     FLB_CONFIG_MAP_INT, "interval_nsec", "0",
     0, FLB_TRUE, offsetof(struct flb_in_probe, interval_nsec),
     "Период проб, наносекунды"
    },
    {0}
};

struct flb_input_plugin in_probe_plugin = {
    .name         = "probe",
    .description  = "Пробы доступности: tcp, icmp, tls, http",
    .cb_init      = cb_init,
    .cb_pre_run   = NULL,
    .cb_collect   = cb_collect,
    .cb_flush_buf = NULL,
    .cb_pause     = cb_pause,
    .cb_resume    = cb_resume,
    .cb_exit      = cb_exit,
    .config_map   = config_map,
    /* пробы блокирующие (connect, рукопожатие, ожидание icmp), поэтому вход
     * работает в своём потоке и не задерживает конвейер */
    .flags        = FLB_INPUT_NET | FLB_INPUT_THREADED,
};
