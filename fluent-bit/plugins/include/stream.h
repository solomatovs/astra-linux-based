/* stream.h — общий протокол пары in_stream / out_stream.
 *
 * Направление данных и направление соединения — РАЗНЫЕ вещи, и на этом здесь
 * всё построено. Имя плагина говорит только про данные, как везде в
 * fluent-bit: `in` принимает, `out` отдаёт. Кто кому звонит — отдельный
 * параметр `Link`:
 *
 *     Link listen     жду подключения на своём порту
 *     Link connect    подключаюсь сам
 *
 * Отсюда две схемы; выбирается та, которую пускает межсетевой экран:
 *
 *     доступ ИЗ центра    out_stream Link listen   ←── in_stream Link connect
 *     доступ В центр      out_stream Link connect  ──→ in_stream Link listen
 *
 * Данные в обоих случаях текут строго out_stream → in_stream. Меняется только
 * то, кто установил соединение.
 *
 * Штатными плагинами так нельзя: все серверные входы fluent-bit только
 * принимают (in_http на GET отвечает «invalid HTTP method»), а все клиентские
 * выходы только подключаются наружу — развязать эти две вещи нечем.
 *
 * Протокол строчный, отлаживается руками через openssl s_client. Роли в
 * рукопожатии зависят не от плагина, а от того, кто звонил: здоровается
 * ВСЕГДА подключившийся, пароль проверяет ВСЕГДА слушающий — то есть тот,
 * чей порт открыт наружу и кого, собственно, и надо защищать.
 *
 *     звонивший →   FLBSTREAM 1 <токен> <имя узла>   «-» вместо пустого
 *     слушавший →   OK              либо  ERR <причина> и разрыв
 *     out_stream →  BATCH <номер> <байт> и следом байты: JSON-строки
 *     in_stream →   ACK <номер>     после того, как записи ушли в конвейер
 *     любой →       PING / PONG     чтобы заметить мёртвый сокет
 *
 * Имя узла нужно там, где слушает in_stream: подключившихся много, и метку
 * источника берём из того, чем узел представился, а не из его адреса.
 *
 * Про гарантии честно: ACK означает «принял в свой конвейер», а не «записано
 * в СУБД» — сквозного подтверждения от выхода ко входу в fluent-bit нет ни у
 * кого. Зато пачка без ACK остаётся чанком fluent-bit, и при storage.type
 * filesystem это диск.
 *
 * TLS сделан напрямую на OpenSSL, как в in_probe: flb_tls_session_create
 * требует внутренний flb_connection, собирать который руками для своего
 * сокета хрупко — структура меняется между версиями. Символы OpenSSL из
 * бинарника экспортированы (проверено nm -D), включая серверную половину:
 * TLS_server_method, SSL_accept, SSL_CTX_use_certificate_chain_file.
 */

#ifndef STREAM_H
#define STREAM_H

#include <fluent-bit/flb_compat.h>

#include <openssl/ssl.h>
#include <openssl/err.h>

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#define STREAM_PROTO      "FLBSTREAM"
#define STREAM_VERSION    1
#define STREAM_LINE_MAX   1024
#define STREAM_NONE       "-"

/* Кто устанавливает соединение. К направлению данных отношения не имеет. */
#define STREAM_LINK_LISTEN   0
#define STREAM_LINK_CONNECT  1

static int stream_link_parse(const char *value)
{
    if (!value) {
        return -1;
    }
    if (strcasecmp(value, "listen") == 0) {
        return STREAM_LINK_LISTEN;
    }
    if (strcasecmp(value, "connect") == 0) {
        return STREAM_LINK_CONNECT;
    }
    return -1;
}

/* Сокет, который может быть обычным или обёрнутым в TLS. Обе стороны работают
 * только через эти функции, чтобы ветка «с шифрованием» не расползлась. */
struct stream_io {
    int fd;
    SSL *ssl;
};

static void stream_io_reset(struct stream_io *io)
{
    io->fd = -1;
    io->ssl = NULL;
}

static void stream_io_close(struct stream_io *io)
{
    if (io->ssl) {
        SSL_shutdown(io->ssl);
        SSL_free(io->ssl);
        io->ssl = NULL;
    }
    if (io->fd >= 0) {
        close(io->fd);
        io->fd = -1;
    }
}

static int stream_io_open(struct stream_io *io)
{
    return io->fd >= 0;
}

/* Таймауты на сокет обязательны: блокирующее чтение с молчащей стороны иначе
 * висит вечно — на этом уже обжигались в in_probe с преамбулами. */
static int stream_set_timeout(int fd, int ms)
{
    struct timeval tv;

    tv.tv_sec = ms / 1000;
    tv.tv_usec = (ms % 1000) * 1000;
    if (setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) != 0) {
        return -1;
    }
    if (setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv)) != 0) {
        return -1;
    }
    return 0;
}

/* Держать соединение живым на уровне ядра: молчащий разрыв (упавший узел,
 * выдернутый кабель) иначе не замечается часами. */
static void stream_set_keepalive(int fd)
{
    int on = 1;

    setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, &on, sizeof(on));
}

static void stream_set_nodelay(int fd)
{
    int on = 1;

    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &on, sizeof(on));
}

static int stream_wait_readable(int fd, int timeout_ms)
{
    struct pollfd pfd;
    int ret;

    pfd.fd = fd;
    pfd.events = POLLIN;
    pfd.revents = 0;

    ret = poll(&pfd, 1, timeout_ms);
    if (ret < 0) {
        return (errno == EINTR) ? 0 : -1;
    }
    return ret;
}

/* Пишет всё до конца: короткая запись в сокет — норма, а не ошибка. */
static int stream_write_all(struct stream_io *io, const void *buf, size_t len)
{
    const char *p = buf;
    size_t sent = 0;
    size_t chunk;
    int ret;

    while (sent < len) {
        if (io->ssl) {
            ret = SSL_write(io->ssl, p + sent, (int) (len - sent));
            if (ret <= 0) {
                return -1;
            }
            sent += (size_t) ret;
        }
        else {
            chunk = len - sent;
            ret = (int) send(io->fd, p + sent, chunk, MSG_NOSIGNAL);
            if (ret <= 0) {
                if (ret < 0 && (errno == EINTR)) {
                    continue;
                }
                return -1;
            }
            sent += (size_t) ret;
        }
    }
    return 0;
}

static int stream_printf(struct stream_io *io, const char *fmt, ...)
{
    char line[STREAM_LINE_MAX];
    va_list ap;
    int len;

    va_start(ap, fmt);
    len = vsnprintf(line, sizeof(line), fmt, ap);
    va_end(ap);

    if (len <= 0 || (size_t) len >= sizeof(line)) {
        return -1;
    }
    return stream_write_all(io, line, (size_t) len);
}

/* Возвращает: >0 — прочитано байт, 0 — данных пока нет, -1 — соединение
 * кончилось. У TLS «данных нет» бывает и при готовом сокете (запись пришла
 * не целиком), поэтому WANT_READ ошибкой не считается. */
static int stream_read_some(struct stream_io *io, void *buf, size_t len)
{
    int ret;
    int err;

    if (io->ssl) {
        ret = SSL_read(io->ssl, buf, (int) len);
        if (ret > 0) {
            return ret;
        }
        err = SSL_get_error(io->ssl, ret);
        if (err == SSL_ERROR_WANT_READ || err == SSL_ERROR_WANT_WRITE) {
            return 0;
        }
        return -1;
    }

    ret = (int) recv(io->fd, buf, len, 0);
    if (ret > 0) {
        return ret;
    }
    if (ret < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)) {
        return 0;
    }
    return -1;
}

/* Есть ли уже разобранное TLS-сообщение в буфере OpenSSL: poll про него не
 * знает, и без этой проверки данные могут пролежать до следующей записи. */
static int stream_pending(struct stream_io *io)
{
    if (io->ssl) {
        return SSL_pending(io->ssl);
    }
    return 0;
}

/* Чтение служебной строки с таймаутом — только для рукопожатия, где обмен
 * строго по очереди. Дальше обе стороны читают потоком. */
static int stream_read_line(struct stream_io *io, char *out, size_t cap,
                            int timeout_ms)
{
    size_t used = 0;
    int ret;
    int waited = 0;
    char c;

    while (used + 1 < cap) {
        if (stream_pending(io) == 0) {
            ret = stream_wait_readable(io->fd, 100);
            if (ret < 0) {
                return -1;
            }
            if (ret == 0) {
                waited += 100;
                if (waited >= timeout_ms) {
                    return -1;
                }
                continue;
            }
        }

        ret = stream_read_some(io, &c, 1);
        if (ret < 0) {
            return -1;
        }
        if (ret == 0) {
            continue;
        }
        if (c == '\n') {
            out[used] = '\0';
            return (int) used;
        }
        if (c != '\r') {
            out[used++] = c;
        }
    }
    return -1;
}

static void stream_ssl_error(char *out, size_t cap)
{
    unsigned long err;

    err = ERR_get_error();
    if (err == 0) {
        snprintf(out, cap, "ошибка TLS");
        return;
    }
    ERR_error_string_n(err, out, cap);
}

/* ------------------------------------------------------------------ TLS --- */

/* Контекст слушающей стороны: свой сертификат и ключ. */
static SSL_CTX *stream_tls_server_ctx(const char *cert, const char *key,
                                      char *err, size_t cap)
{
    SSL_CTX *ctx;

    if (!cert || !key) {
        snprintf(err, cap, "Secure On требует Cert_File и Key_File");
        return NULL;
    }
    ctx = SSL_CTX_new(TLS_server_method());
    if (!ctx) {
        stream_ssl_error(err, cap);
        return NULL;
    }
    if (SSL_CTX_use_certificate_chain_file(ctx, cert) != 1 ||
        SSL_CTX_use_PrivateKey_file(ctx, key, SSL_FILETYPE_PEM) != 1) {
        stream_ssl_error(err, cap);
        SSL_CTX_free(ctx);
        return NULL;
    }
    if (SSL_CTX_check_private_key(ctx) != 1) {
        snprintf(err, cap, "ключ не подходит к сертификату");
        SSL_CTX_free(ctx);
        return NULL;
    }
    return ctx;
}

/* Контекст подключающейся стороны: чем проверять чужой сертификат. */
static SSL_CTX *stream_tls_client_ctx(const char *ca, char *err, size_t cap)
{
    SSL_CTX *ctx;

    ctx = SSL_CTX_new(TLS_client_method());
    if (!ctx) {
        stream_ssl_error(err, cap);
        return NULL;
    }
    if (ca && strlen(ca) > 0) {
        if (SSL_CTX_load_verify_locations(ctx, ca, NULL) != 1) {
            stream_ssl_error(err, cap);
            SSL_CTX_free(ctx);
            return NULL;
        }
    }
    else {
        SSL_CTX_set_default_verify_paths(ctx);
    }
    return ctx;
}

/* ------------------------------------------------------------- соединение - */

static int stream_connect(const char *host, int port, int timeout_ms)
{
    struct addrinfo hints;
    struct addrinfo *res = NULL;
    struct addrinfo *rp;
    struct pollfd pfd;
    char port_str[16];
    int fd = -1;
    int flags;
    int err;
    socklen_t len;
    int ret;

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    snprintf(port_str, sizeof(port_str), "%i", port);

    if (getaddrinfo(host, port_str, &hints, &res) != 0) {
        return -1;
    }

    for (rp = res; rp; rp = rp->ai_next) {
        fd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (fd < 0) {
            continue;
        }
        flags = fcntl(fd, F_GETFL, 0);
        fcntl(fd, F_SETFL, flags | O_NONBLOCK);

        ret = connect(fd, rp->ai_addr, rp->ai_addrlen);
        if (ret != 0 && errno == EINPROGRESS) {
            pfd.fd = fd;
            pfd.events = POLLOUT;
            pfd.revents = 0;
            ret = poll(&pfd, 1, timeout_ms);
            if (ret > 0) {
                err = 0;
                len = sizeof(err);
                getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &len);
                ret = err == 0 ? 0 : -1;
            }
            else {
                ret = -1;
            }
        }
        if (ret == 0) {
            fcntl(fd, F_SETFL, flags);
            stream_set_timeout(fd, timeout_ms);
            stream_set_keepalive(fd);
            stream_set_nodelay(fd);
            freeaddrinfo(res);
            return fd;
        }
        close(fd);
        fd = -1;
    }

    freeaddrinfo(res);
    return -1;
}

static int stream_listen(const char *host, int port, int backlog)
{
    struct sockaddr_in addr;
    int fd;
    int on = 1;

    fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        return -1;
    }
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons((uint16_t) port);
    if (!host || strlen(host) == 0 || strcmp(host, "0.0.0.0") == 0) {
        addr.sin_addr.s_addr = htonl(INADDR_ANY);
    }
    else if (inet_pton(AF_INET, host, &addr.sin_addr) != 1) {
        close(fd);
        return -1;
    }

    if (bind(fd, (struct sockaddr *) &addr, sizeof(addr)) != 0 ||
        listen(fd, backlog) != 0) {
        close(fd);
        return -1;
    }
    return fd;
}

static void stream_peer_name(int fd, char *out, size_t cap)
{
    struct sockaddr_in addr;
    socklen_t len = sizeof(addr);
    char ip[INET_ADDRSTRLEN];

    if (getpeername(fd, (struct sockaddr *) &addr, &len) == 0 &&
        inet_ntop(AF_INET, &addr.sin_addr, ip, sizeof(ip))) {
        snprintf(out, cap, "%s:%i", ip, ntohs(addr.sin_port));
        return;
    }
    snprintf(out, cap, "?");
}

/* ----------------------------------------------------------- рукопожатие -- */

/* Токен сравнивается целиком и без раннего выхода: не даём измерить длину
 * совпадающего префикса по времени ответа. */
static int stream_token_equal(const char *a, const char *b)
{
    size_t la = a ? strlen(a) : 0;
    size_t lb = b ? strlen(b) : 0;
    size_t i;
    unsigned char diff = 0;

    if (la != lb) {
        return 0;
    }
    for (i = 0; i < la; i++) {
        diff |= (unsigned char) (a[i] ^ b[i]);
    }
    return diff == 0;
}

/* Здоровается тот, кто звонил, — независимо от того, отдаёт он данные или
 * принимает. Возвращает 0, если та сторона ответила OK. */
static int stream_hello_send(struct stream_io *io, const char *token,
                             const char *node, int timeout_ms,
                             char *reason, size_t reason_cap)
{
    char line[STREAM_LINE_MAX];

    if (stream_printf(io, "%s %i %s %s\n", STREAM_PROTO, STREAM_VERSION,
                      (token && strlen(token) > 0) ? token : STREAM_NONE,
                      (node && strlen(node) > 0) ? node : STREAM_NONE) != 0) {
        snprintf(reason, reason_cap, "рукопожатие не ушло");
        return -1;
    }
    if (stream_read_line(io, line, sizeof(line), timeout_ms) < 0) {
        snprintf(reason, reason_cap,
                 "ответа на рукопожатие нет (порт занят другим?)");
        return -1;
    }
    if (strcmp(line, "OK") != 0) {
        snprintf(reason, reason_cap, "%s", line);
        return -1;
    }
    return 0;
}

/* Проверяет тот, кто слушал: его порт открыт наружу, ему и спрашивать пароль.
 * В node_out кладётся имя, которым представился подключившийся. */
static int stream_hello_check(struct stream_io *io, const char *token,
                              int timeout_ms, char *node_out, size_t node_cap,
                              char *reason, size_t reason_cap)
{
    char line[STREAM_LINE_MAX];
    char proto[32];
    char got_token[STREAM_LINE_MAX];
    char got_node[STREAM_LINE_MAX];
    int version = 0;
    int n;

    if (stream_read_line(io, line, sizeof(line), timeout_ms) < 0) {
        snprintf(reason, reason_cap, "строка не пришла за %i мс", timeout_ms);
        return -1;
    }

    got_token[0] = '\0';
    got_node[0] = '\0';
    n = sscanf(line, "%31s %i %1023s %1023s", proto, &version, got_token,
               got_node);
    if (n < 2 || strcmp(proto, STREAM_PROTO) != 0) {
        stream_printf(io, "ERR ожидалось %s\n", STREAM_PROTO);
        snprintf(reason, reason_cap, "чужой протокол (%s)", line);
        return -1;
    }
    if (version != STREAM_VERSION) {
        stream_printf(io, "ERR версия %i не поддержана\n", version);
        snprintf(reason, reason_cap, "версия %i", version);
        return -1;
    }
    if (token && strlen(token) > 0 && !stream_token_equal(got_token, token)) {
        stream_printf(io, "ERR токен не подошёл\n");
        snprintf(reason, reason_cap, "токен не подошёл");
        return -1;
    }
    if (stream_printf(io, "OK\n") != 0) {
        snprintf(reason, reason_cap, "ответ OK не ушёл");
        return -1;
    }

    if (node_out && node_cap > 0) {
        if (strlen(got_node) > 0 && strcmp(got_node, STREAM_NONE) != 0) {
            snprintf(node_out, node_cap, "%s", got_node);
        }
        else {
            node_out[0] = '\0';
        }
    }
    return 0;
}

#endif
