/* in_stream — вход, который ПРИНИМАЕТ записи по установленному соединению.
 *
 * Имя говорит только про направление ДАННЫХ: как и всякий in в fluent-bit,
 * этот плагин принимает. Кто устанавливает соединение — выводится из самой
 * конфигурации, отдельного переключателя нет:
 *
 *     задан Targets  сам хожу к отправителям. Нужно там, где доступ есть
 *                    только ИЗ центра
 *     задан Listen   жду, когда отправители придут ко мне — обычная схема,
 *                    когда наружу пускают периферию
 *
 * Одно другому не мешает: можно задать и то, и другое, и тогда часть узлов
 * обзванивается, а часть приходит сама. Данные в любом случае текут от
 * out_stream сюда. Протокол — plugins/include/stream.h.
 *
 *   [INPUT]                        [INPUT]
 *       Name    stream                 Name    stream
 *       Tag     edge.logs              Tag     edge.logs
 *       Targets edge-1:10000           Listen  0.0.0.0
 *       Token   секрет                 Port    10000
 *                                      Token   секрет
 *
 * Штатной заменой это не делается: все серверные входы fluent-bit только
 * принимают (in_http на GET отвечает «invalid HTTP method»), а единственный
 * входящий клиент — prometheus_scrape — умеет только метрики.
 *
 * Всё держится на одном потоке плагина: коллектор тикает каждые Poll_Ms,
 * опрашивает сокеты через poll и разбирает пришедшее. Отдельных потоков на
 * узел нет, поэтому нет и блокировок — записи кладёт тот же поток, который
 * читает. Вход помечен FLB_INPUT_THREADED, чтобы медленный узел не держал
 * общий конвейер.
 *
 * Загрузчик ищет в .so структуру `in_stream_plugin`.
 */

#include <fluent-bit/flb_input_plugin.h>
#include <fluent-bit/flb_config_map.h>
#include <fluent-bit/flb_pack.h>
#include <fluent-bit/flb_sds.h>
#include <fluent-bit/flb_time.h>
#include <fluent-bit/flb_log_event_encoder.h>

#include "dbmetrics.h"
#include "stream.h"

#define PEERS_MAX        64
#define READ_CHUNK       65536

/* Состояние разбора: ждём служебную строку или дочитываем тело пачки. */
#define ST_LINE   0
#define ST_BODY   1

/* Узел на том конце. При Link connect слоты заполняются из Targets заранее,
 * при Link listen — по мере того, как узлы приходят сами. */
struct stream_peer {
    int used;
    int dial;                   /* узел из Targets: его переподключаем сами */
    char name[256];             /* метка источника */
    char host[256];             /* только при Link connect */
    int port;

    struct stream_io io;
    int ready;
    time_t next_try;

    /* буфер приёма: служебные строки и тело пачки лежат в нём вперемешку */
    char *buf;
    size_t cap;
    size_t used_bytes;

    int state;
    uint64_t seq;
    size_t need;

    uint64_t records;
    uint64_t batches;
};

struct flb_in_stream {
    /* настройки */
    flb_sds_t targets_conf;
    flb_sds_t token;
    flb_sds_t node;
    int poll_ms;
    int retry_pause_sec;
    int connect_timeout_ms;
    int handshake_ms;
    size_t batch_max;
    char *source_key;
    char *time_key;
    int secure;
    int verify;
    flb_sds_t ca_file;
    flb_sds_t servername;
    flb_sds_t cert_file;
    flb_sds_t key_file;

    struct stream_peer peers[PEERS_MAX];
    int peer_count;             /* только при Link connect: сколько целей */

    int listen_fd;              /* только при Link listen */
    SSL_CTX *ssl_ctx;

    int coll_id;
    struct flb_log_event_encoder *encoder;
    struct flb_input_instance *ins;
};

/* ------------------------------------------------------------- разбор ---- */

static int targets_parse(struct flb_in_stream *ctx)
{
    char *copy;
    char *save = NULL;
    char *item;
    char *colon;
    struct stream_peer *t;

    if (!ctx->targets_conf || flb_sds_len(ctx->targets_conf) == 0) {
        return -1;
    }
    copy = flb_strdup(ctx->targets_conf);
    if (!copy) {
        return -1;
    }

    item = strtok_r(copy, ",", &save);
    while (item && ctx->peer_count < PEERS_MAX) {
        while (*item == ' ' || *item == '\t') {
            item++;
        }
        if (*item == '\0') {
            item = strtok_r(NULL, ",", &save);
            continue;
        }

        t = &ctx->peers[ctx->peer_count];
        memset(t, 0, sizeof(*t));
        stream_io_reset(&t->io);
        t->used = FLB_TRUE;
        t->dial = FLB_TRUE;

        snprintf(t->name, sizeof(t->name), "%s", item);
        /* хвостовые пробелы имени портят метку источника */
        while (strlen(t->name) > 0 &&
               (t->name[strlen(t->name) - 1] == ' ' ||
                t->name[strlen(t->name) - 1] == '\t')) {
            t->name[strlen(t->name) - 1] = '\0';
        }

        snprintf(t->host, sizeof(t->host), "%s", t->name);
        colon = strrchr(t->host, ':');
        if (colon) {
            *colon = '\0';
            t->port = atoi(colon + 1);
        }
        if (t->port <= 0) {
            t->port = 10000;
        }

        ctx->peer_count++;
        item = strtok_r(NULL, ",", &save);
    }

    flb_free(copy);
    return ctx->peer_count > 0 ? 0 : -1;
}

static int buf_reserve(struct stream_peer *t, size_t extra)
{
    size_t want = t->used_bytes + extra;
    char *tmp;

    if (want <= t->cap) {
        return 0;
    }
    while (t->cap < want) {
        t->cap = t->cap ? t->cap * 2 : READ_CHUNK;
    }
    tmp = flb_realloc(t->buf, t->cap);
    if (!tmp) {
        return -1;
    }
    t->buf = tmp;
    return 0;
}

/* Слот узла из Targets остаётся за ним — его переподключим сами. Слот
 * пришедшего узла освобождается: он придёт снова, когда захочет. */
static void peer_drop(struct flb_in_stream *ctx, struct stream_peer *t,
                      const char *why)
{
    if (stream_io_open(&t->io)) {
        flb_plg_warn(ctx->ins, "%s: соединение закрыто (%s)", t->name, why);
    }
    stream_io_close(&t->io);
    t->ready = FLB_FALSE;
    t->used_bytes = 0;
    t->state = ST_LINE;
    t->need = 0;

    if (t->dial) {
        t->next_try = time(NULL) + ctx->retry_pause_sec;
    }
    else {
        flb_free(t->buf);
        t->buf = NULL;
        t->cap = 0;
        t->used = FLB_FALSE;
    }
}

/* --------------------------------------------------------- подключение --- */

/* Link connect: звоним периферии и представляемся. */
static int peer_dial(struct flb_in_stream *ctx, struct stream_peer *t)
{
    char err[256];
    char reason[256];
    int fd;

    fd = stream_connect(t->host, t->port, ctx->connect_timeout_ms);
    if (fd < 0) {
        flb_plg_debug(ctx->ins, "%s: подключиться не удалось", t->name);
        t->next_try = time(NULL) + ctx->retry_pause_sec;
        return -1;
    }

    stream_io_reset(&t->io);
    t->io.fd = fd;

    if (ctx->secure) {
        t->io.ssl = SSL_new(ctx->ssl_ctx);
        if (!t->io.ssl || SSL_set_fd(t->io.ssl, fd) != 1) {
            peer_drop(ctx, t, "сессия TLS не создана");
            return -1;
        }
        SSL_set_tlsext_host_name(t->io.ssl,
            (ctx->servername && flb_sds_len(ctx->servername) > 0) ?
            ctx->servername : t->host);
        if (SSL_connect(t->io.ssl) != 1) {
            stream_ssl_error(err, sizeof(err));
            flb_plg_warn(ctx->ins, "%s: TLS не установлен: %s", t->name, err);
            peer_drop(ctx, t, "TLS");
            return -1;
        }
        if (ctx->verify && SSL_get_verify_result(t->io.ssl) != X509_V_OK) {
            flb_plg_warn(ctx->ins, "%s: сертификат не проверен", t->name);
            peer_drop(ctx, t, "сертификат");
            return -1;
        }
    }

    if (stream_hello_send(&t->io, ctx->token, ctx->node, ctx->handshake_ms,
                          reason, sizeof(reason)) != 0) {
        flb_plg_error(ctx->ins, "%s: отказано: %s", t->name, reason);
        peer_drop(ctx, t, "рукопожатие");
        return -1;
    }

    t->ready = FLB_TRUE;
    t->state = ST_LINE;
    t->used_bytes = 0;
    flb_plg_info(ctx->ins, "%s: подключён%s", t->name,
                 ctx->secure ? " (TLS)" : "");
    return 0;
}

/* Link listen: принимаем пришедшую периферию и спрашиваем у неё пароль. */
static void peer_accept(struct flb_in_stream *ctx)
{
    struct stream_io io;
    struct stream_peer *t = NULL;
    char err[256];
    char node[256];
    char reason[256];
    char addr[64];
    int fd;
    int i;

    if (stream_wait_readable(ctx->listen_fd, 0) <= 0) {
        return;
    }
    fd = accept(ctx->listen_fd, NULL, NULL);
    if (fd < 0) {
        return;
    }
    stream_set_timeout(fd, ctx->handshake_ms);
    stream_set_keepalive(fd);
    stream_set_nodelay(fd);

    stream_peer_name(fd, addr, sizeof(addr));

    for (i = 0; i < PEERS_MAX; i++) {
        if (!ctx->peers[i].used) {
            t = &ctx->peers[i];
            break;
        }
    }
    if (!t) {
        close(fd);
        flb_plg_warn(ctx->ins, "%s отклонён: занято все %i мест",
                     addr, PEERS_MAX);
        return;
    }

    stream_io_reset(&io);
    io.fd = fd;

    if (ctx->secure) {
        io.ssl = SSL_new(ctx->ssl_ctx);
        if (!io.ssl || SSL_set_fd(io.ssl, fd) != 1 || SSL_accept(io.ssl) != 1) {
            stream_ssl_error(err, sizeof(err));
            flb_plg_warn(ctx->ins, "%s: TLS не установлен: %s", addr, err);
            stream_io_close(&io);
            return;
        }
    }

    node[0] = '\0';
    if (stream_hello_check(&io, ctx->token, ctx->handshake_ms,
                           node, sizeof(node), reason, sizeof(reason)) != 0) {
        flb_plg_warn(ctx->ins, "%s отклонён: %s", addr, reason);
        stream_io_close(&io);
        return;
    }

    memset(t, 0, sizeof(*t));
    t->used = FLB_TRUE;
    t->dial = FLB_FALSE;
    t->io = io;
    t->ready = FLB_TRUE;
    t->state = ST_LINE;
    /* метка источника — то, чем узел представился; адрес только как запасной
     * вариант: он меняется от подключения к подключению */
    snprintf(t->name, sizeof(t->name), "%s",
             strlen(node) > 0 ? node : addr);

    flb_plg_info(ctx->ins, "%s подключился с %s%s", t->name, addr,
                 ctx->secure ? " (TLS)" : "");
}

/* ------------------------------------------------------------- записи ---- */

/* Одна JSON-строка — одна запись в конвейере. Поля исходной записи
 * переносятся как есть, сверху добавляется метка источника. */
static int emit_record(struct flb_in_stream *ctx, struct stream_peer *t,
                       const char *json, size_t len)
{
    msgpack_unpacked result;
    msgpack_object *body;
    msgpack_object *key;
    msgpack_object *val;
    struct flb_time tm;
    size_t off = 0;
    char *mp = NULL;
    size_t mp_size = 0;
    int type = 0;
    int taken = FLB_FALSE;
    int i;
    int ret;

    if (flb_pack_json(json, len, &mp, &mp_size, &type, NULL) != 0) {
        flb_plg_warn(ctx->ins, "%s: строка не разобрана как JSON", t->name);
        return -1;
    }

    msgpack_unpacked_init(&result);
    if (msgpack_unpack_next(&result, mp, mp_size, &off) != MSGPACK_UNPACK_SUCCESS) {
        msgpack_unpacked_destroy(&result);
        flb_free(mp);
        return -1;
    }
    body = &result.data;
    if (body->type != MSGPACK_OBJECT_MAP) {
        msgpack_unpacked_destroy(&result);
        flb_free(mp);
        flb_plg_warn(ctx->ins, "%s: строка не объект JSON", t->name);
        return -1;
    }

    flb_time_get(&tm);

    ret = flb_log_event_encoder_begin_record(ctx->encoder);

    for (i = 0; i < (int) body->via.map.size && ret == 0; i++) {
        key = &body->via.map.ptr[i].key;
        val = &body->via.map.ptr[i].val;

        /* время события: берём из поля, которое проставил отправитель, иначе
         * в ClickHouse приедет время приёма, а не время строки лога.
         *
         * Разобранное поле в тело НЕ копируем. Время у события своё, отдельно
         * от полей, и выход на той стороне добавит его сам — оставь мы копию,
         * в JSON оказалось бы два ключа timestamp, и ClickHouse отвечает на
         * это «Duplicate field found while parsing JSONEachRow» (проверено).
         * Не разобралось — поле остаётся как есть, терять его незачем. */
        if (!taken && ctx->time_key && strlen(ctx->time_key) > 0 &&
            key->type == MSGPACK_OBJECT_STR &&
            key->via.str.size == strlen(ctx->time_key) &&
            strncmp(key->via.str.ptr, ctx->time_key, key->via.str.size) == 0) {
            if (val->type == MSGPACK_OBJECT_STR &&
                dbm_time(val->via.str.ptr, val->via.str.size, &tm) == 0) {
                taken = FLB_TRUE;
                continue;
            }
            if (val->type == MSGPACK_OBJECT_FLOAT ||
                val->type == MSGPACK_OBJECT_FLOAT32) {
                tm.tm.tv_sec = (long) val->via.f64;
                tm.tm.tv_nsec = (long) ((val->via.f64 - tm.tm.tv_sec) * 1e9);
                taken = FLB_TRUE;
                continue;
            }
        }

        ret = flb_log_event_encoder_append_body_msgpack_object(ctx->encoder, key);
        if (ret == 0) {
            ret = flb_log_event_encoder_append_body_msgpack_object(ctx->encoder,
                                                                  val);
        }
    }

    if (ret == 0 && ctx->source_key && strlen(ctx->source_key) > 0) {
        ret = flb_log_event_encoder_append_body_values(
                ctx->encoder,
                FLB_LOG_EVENT_CSTRING_VALUE(ctx->source_key),
                FLB_LOG_EVENT_CSTRING_VALUE(t->name));
    }

    if (ret == 0) {
        ret = flb_log_event_encoder_set_timestamp(ctx->encoder, &tm);
    }
    if (ret == 0) {
        ret = flb_log_event_encoder_commit_record(ctx->encoder);
    }
    else {
        flb_log_event_encoder_rollback_record(ctx->encoder);
    }

    msgpack_unpacked_destroy(&result);
    flb_free(mp);

    return ret == 0 ? 0 : -1;
}

/* Тело пачки: JSON-строки через '\n'. Переводы строк внутри значений
 * экранированы (см. dbsink.h), поэтому граница строки = граница записи. */
static int emit_batch(struct flb_in_stream *ctx, struct stream_peer *t,
                      const char *body, size_t len)
{
    const char *line = body;
    const char *end = body + len;
    const char *nl;
    int rows = 0;

    while (line < end) {
        nl = memchr(line, '\n', (size_t) (end - line));
        if (!nl) {
            nl = end;
        }
        if (nl > line && emit_record(ctx, t, line, (size_t) (nl - line)) == 0) {
            rows++;
        }
        line = nl + 1;
    }
    return rows;
}

/* Разбор накопленного буфера. Возвращает -1, если соединение пора рвать. */
static int peer_process(struct flb_in_stream *ctx, struct stream_peer *t)
{
    char *nl;
    size_t line_len;
    unsigned long long seq;
    unsigned long long bytes;
    int rows;

    while (1) {
        if (t->state == ST_LINE) {
            nl = memchr(t->buf, '\n', t->used_bytes);
            if (!nl) {
                if (t->used_bytes > STREAM_LINE_MAX) {
                    flb_plg_error(ctx->ins, "%s: служебная строка без конца",
                                  t->name);
                    return -1;
                }
                return 0;
            }
            line_len = (size_t) (nl - t->buf);
            *nl = '\0';

            if (sscanf(t->buf, "BATCH %llu %llu", &seq, &bytes) == 2) {
                if (bytes > ctx->batch_max) {
                    flb_plg_error(ctx->ins, "%s: пачка %llu байт больше "
                                  "Batch_Max_Bytes", t->name, bytes);
                    return -1;
                }
                t->seq = (uint64_t) seq;
                t->need = (size_t) bytes;
                t->state = ST_BODY;
            }
            else if (strcmp(t->buf, "PING") == 0) {
                if (stream_printf(&t->io, "PONG\n") != 0) {
                    return -1;
                }
            }
            else if (strcmp(t->buf, "PONG") == 0) {
                /* ничего */
            }
            else if (line_len > 0) {
                flb_plg_debug(ctx->ins, "%s: непонятная строка: %s",
                              t->name, t->buf);
            }

            memmove(t->buf, nl + 1, t->used_bytes - line_len - 1);
            t->used_bytes -= line_len + 1;
            continue;
        }

        /* ST_BODY: ждём тело целиком, иначе резать по '\n' нельзя */
        if (t->used_bytes < t->need) {
            return 0;
        }

        rows = emit_batch(ctx, t, t->buf, t->need);
        t->records += (uint64_t) rows;
        t->batches++;

        /* подтверждаем только после того, как записи ушли в конвейер */
        if (flb_input_log_append(ctx->ins, NULL, 0,
                                 ctx->encoder->output_buffer,
                                 ctx->encoder->output_length) != 0) {
            flb_plg_error(ctx->ins, "%s: записи не приняты конвейером", t->name);
            flb_log_event_encoder_reset(ctx->encoder);
            return -1;
        }
        flb_log_event_encoder_reset(ctx->encoder);

        if (stream_printf(&t->io, "ACK %llu\n",
                          (unsigned long long) t->seq) != 0) {
            flb_plg_warn(ctx->ins, "%s: подтверждение не ушло", t->name);
            return -1;
        }

        flb_plg_debug(ctx->ins, "%s: пачка %llu, записей %i, всего %llu",
                      t->name, (unsigned long long) t->seq, rows,
                      (unsigned long long) t->records);

        memmove(t->buf, t->buf + t->need, t->used_bytes - t->need);
        t->used_bytes -= t->need;
        t->need = 0;
        t->state = ST_LINE;
    }
}

static int peer_read(struct flb_in_stream *ctx, struct stream_peer *t)
{
    int ret;

    while (1) {
        if (stream_pending(&t->io) == 0) {
            ret = stream_wait_readable(t->io.fd, 0);
            if (ret < 0) {
                return -1;
            }
            if (ret == 0) {
                return 0;
            }
        }

        if (buf_reserve(t, READ_CHUNK) != 0) {
            return -1;
        }
        ret = stream_read_some(&t->io, t->buf + t->used_bytes, READ_CHUNK);
        if (ret < 0) {
            return -1;
        }
        if (ret == 0) {
            return 0;
        }
        t->used_bytes += (size_t) ret;

        if (peer_process(ctx, t) != 0) {
            return -1;
        }
    }
}

static int cb_collect(struct flb_input_instance *ins, struct flb_config *config,
                      void *in_context)
{
    struct flb_in_stream *ctx = in_context;
    struct stream_peer *t;
    time_t now;
    int i;

    (void) ins;
    (void) config;

    now = time(NULL);

    if (ctx->listen_fd >= 0) {
        peer_accept(ctx);
    }

    for (i = 0; i < PEERS_MAX; i++) {
        t = &ctx->peers[i];
        if (!t->used) {
            continue;
        }
        if (!stream_io_open(&t->io)) {
            if (t->dial && now >= t->next_try) {
                peer_dial(ctx, t);
            }
            continue;
        }
        if (!t->ready) {
            continue;
        }
        if (peer_read(ctx, t) != 0) {
            peer_drop(ctx, t, "чтение");
        }
    }

    return 0;
}

/* -------------------------------------------------------------- плагин --- */

static int cb_init(struct flb_input_instance *ins, struct flb_config *config,
                   void *data)
{
    struct flb_in_stream *ctx;
    const char *listen_addr;
    char err[256];
    int do_listen;
    int do_dial;
    int port = 0;
    int i;

    (void) data;

    ctx = flb_calloc(1, sizeof(struct flb_in_stream));
    if (!ctx) {
        flb_errno();
        return -1;
    }
    ctx->ins = ins;
    ctx->listen_fd = -1;
    for (i = 0; i < PEERS_MAX; i++) {
        stream_io_reset(&ctx->peers[i].io);
    }

    if (flb_input_config_map_set(ins, (void *) ctx) == -1) {
        flb_free(ctx);
        return -1;
    }

    /* Режим выводится из конфигурации: задан Targets — обзваниваем, задан
     * Listen — ждём. Одно другому не мешает, можно и вместе. */
    listen_addr = ins->host.listen;
    do_listen = (listen_addr && strlen(listen_addr) > 0);
    do_dial = (ctx->targets_conf && flb_sds_len(ctx->targets_conf) > 0);

    if (!do_listen && !do_dial) {
        flb_plg_error(ins, "укажите Targets (ходить самому) или Listen "
                      "(ждать подключения)");
        flb_free(ctx);
        return -1;
    }
    if (do_dial && targets_parse(ctx) != 0) {
        flb_plg_error(ins, "Targets не разобран: ожидается host:port через "
                      "запятую");
        flb_free(ctx);
        return -1;
    }

    /* Слушающему нужен свой сертификат, звонящему — чем проверять чужой.
     * Когда включено и то, и другое, TLS поддержан только у одной роли:
     * контекст OpenSSL один, и смешивать их незачем. */
    if (ctx->secure) {
        if (do_listen) {
            ctx->ssl_ctx = stream_tls_server_ctx(ctx->cert_file, ctx->key_file,
                                                 err, sizeof(err));
            if (do_dial) {
                flb_plg_warn(ins, "TLS настроен для приёма; исходящие "
                             "подключения пойдут без него");
            }
        }
        else {
            ctx->ssl_ctx = stream_tls_client_ctx(ctx->ca_file, err, sizeof(err));
        }
        if (!ctx->ssl_ctx) {
            flb_plg_error(ins, "TLS: %s", err);
            flb_free(ctx);
            return -1;
        }
    }

    if (do_listen) {
        port = ins->host.port > 0 ? ins->host.port : 10000;
        ctx->listen_fd = stream_listen(listen_addr, port, PEERS_MAX);
        if (ctx->listen_fd < 0) {
            flb_plg_error(ins, "порт %i не занят под приём: %s", port,
                          strerror(errno));
            if (ctx->ssl_ctx) {
                SSL_CTX_free(ctx->ssl_ctx);
            }
            flb_free(ctx);
            return -1;
        }
    }

    ctx->encoder = flb_log_event_encoder_create(FLB_LOG_EVENT_FORMAT_DEFAULT);
    if (!ctx->encoder) {
        flb_plg_error(ins, "кодировщик событий не создан");
        if (ctx->listen_fd >= 0) {
            close(ctx->listen_fd);
        }
        if (ctx->ssl_ctx) {
            SSL_CTX_free(ctx->ssl_ctx);
        }
        flb_free(ctx);
        return -1;
    }

    flb_input_set_context(ins, ctx);

    ctx->coll_id = flb_input_set_collector_time(ins, cb_collect,
                                                0, ctx->poll_ms * 1000000L,
                                                config);
    if (ctx->coll_id < 0) {
        flb_plg_error(ins, "коллектор не заведён");
        flb_log_event_encoder_destroy(ctx->encoder);
        if (ctx->listen_fd >= 0) {
            close(ctx->listen_fd);
        }
        if (ctx->ssl_ctx) {
            SSL_CTX_free(ctx->ssl_ctx);
        }
        flb_free(ctx);
        return -1;
    }

    if (do_dial) {
        flb_plg_info(ins, "подключаюсь к отправителям: целей %i%s%s, "
                     "опрос каждые %i мс", ctx->peer_count,
                     ctx->secure && !do_listen ? ", TLS" : "",
                     (ctx->token && flb_sds_len(ctx->token) > 0) ?
                         ", по токену" : "",
                     ctx->poll_ms);
    }
    if (do_listen) {
        flb_plg_info(ins, "жду отправителей на %s:%i%s%s, мест %i",
                     listen_addr, ins->host.port > 0 ? ins->host.port : 10000,
                     ctx->secure ? ", TLS" : "",
                     (ctx->token && flb_sds_len(ctx->token) > 0) ?
                         ", по токену" : "",
                     PEERS_MAX - ctx->peer_count);
    }
    return 0;
}

static int cb_exit(void *data, struct flb_config *config)
{
    struct flb_in_stream *ctx = data;
    int i;

    (void) config;

    if (!ctx) {
        return 0;
    }
    for (i = 0; i < PEERS_MAX; i++) {
        stream_io_close(&ctx->peers[i].io);
        flb_free(ctx->peers[i].buf);
    }
    if (ctx->listen_fd >= 0) {
        close(ctx->listen_fd);
    }
    if (ctx->encoder) {
        flb_log_event_encoder_destroy(ctx->encoder);
    }
    if (ctx->ssl_ctx) {
        SSL_CTX_free(ctx->ssl_ctx);
    }
    flb_free(ctx);
    return 0;
}

static struct flb_config_map config_map[] = {
    {
     FLB_CONFIG_MAP_STR, "targets", NULL,
     0, FLB_TRUE, offsetof(struct flb_in_stream, targets_conf),
     "Отправители через запятую: host:port. Заданы — ходим к ним сами"
    },
    {
     FLB_CONFIG_MAP_STR, "token", NULL,
     0, FLB_TRUE, offsetof(struct flb_in_stream, token),
     "Пароль: у пришедших спрашиваем, при своём подключении предъявляем"
    },
    {
     FLB_CONFIG_MAP_STR, "node", NULL,
     0, FLB_TRUE, offsetof(struct flb_in_stream, node),
     "Имя, которым представляемся, когда подключаемся сами"
    },
    {
     FLB_CONFIG_MAP_STR, "source_key", "source",
     0, FLB_TRUE, offsetof(struct flb_in_stream, source_key),
     "Поле с именем отправителя; пустое — не добавлять"
    },
    {
     FLB_CONFIG_MAP_STR, "time_key", "timestamp",
     0, FLB_TRUE, offsetof(struct flb_in_stream, time_key),
     "Поле со временем события; пустое — ставить время приёма"
    },
    {
     FLB_CONFIG_MAP_INT, "poll_ms", "200",
     0, FLB_TRUE, offsetof(struct flb_in_stream, poll_ms),
     "Период опроса сокетов; на задержку данных влияет слабо"
    },
    {
     FLB_CONFIG_MAP_INT, "retry_pause_sec", "10",
     0, FLB_TRUE, offsetof(struct flb_in_stream, retry_pause_sec),
     "Пауза перед повторным подключением к недоступной цели"
    },
    {
     FLB_CONFIG_MAP_INT, "connect_timeout_ms", "5000",
     0, FLB_TRUE, offsetof(struct flb_in_stream, connect_timeout_ms),
     "Таймаут своего подключения"
    },
    {
     FLB_CONFIG_MAP_INT, "handshake_ms", "5000",
     0, FLB_TRUE, offsetof(struct flb_in_stream, handshake_ms),
     "Таймаут рукопожатия"
    },
    {
     FLB_CONFIG_MAP_SIZE, "batch_max_bytes", "64M",
     0, FLB_TRUE, offsetof(struct flb_in_stream, batch_max),
     "Предел на размер пачки: защита от испорченного заголовка"
    },
    {
     FLB_CONFIG_MAP_BOOL, "secure", "false",
     0, FLB_TRUE, offsetof(struct flb_in_stream, secure),
     "TLS: у слушающего свой сертификат, у звонящего — проверка чужого"
    },
    {
     FLB_CONFIG_MAP_BOOL, "verify", "true",
     0, FLB_TRUE, offsetof(struct flb_in_stream, verify),
     "Считать непроверенный сертификат ошибкой (когда звоним сами)"
    },
    {
     FLB_CONFIG_MAP_STR, "ca_file", NULL,
     0, FLB_TRUE, offsetof(struct flb_in_stream, ca_file),
     "Корневой сертификат для проверки той стороны (когда звоним сами)"
    },
    {
     FLB_CONFIG_MAP_STR, "servername", NULL,
     0, FLB_TRUE, offsetof(struct flb_in_stream, servername),
     "Имя для SNI, если отличается от адреса"
    },
    {
     FLB_CONFIG_MAP_STR, "cert_file", NULL,
     0, FLB_TRUE, offsetof(struct flb_in_stream, cert_file),
     "Свой сертификат в PEM (нужен, когда слушаем)"
    },
    {
     FLB_CONFIG_MAP_STR, "key_file", NULL,
     0, FLB_TRUE, offsetof(struct flb_in_stream, key_file),
     "Свой закрытый ключ в PEM (нужен, когда слушаем)"
    },
    {0}
};

struct flb_input_plugin in_stream_plugin = {
    .name         = "stream",
    .description  = "Принимает записи; Targets — ходим сами, Listen — ждём",
    .cb_init      = cb_init,
    .cb_pre_run   = NULL,
    .cb_collect   = cb_collect,
    .cb_flush_buf = NULL,
    .cb_pause     = NULL,
    .cb_resume    = NULL,
    .cb_exit      = cb_exit,
    .config_map   = config_map,
    .flags        = FLB_INPUT_THREADED,
};
