/* out_stream — выход, который ОТДАЁТ записи по установленному соединению.
 *
 * Имя говорит только про направление ДАННЫХ: как и всякий out в fluent-bit,
 * этот плагин отдаёт. Кто устанавливает соединение — выводится из самой
 * конфигурации, отдельного переключателя нет:
 *
 *     задан Listen   слушаю Listen:Port и жду, когда за логами придут.
 *                    Нужно там, где доступ есть только ИЗ центра
 *     задан Host     сам подключаюсь к Host:Port получателя — обычная схема,
 *                    когда наружу пускают периферию
 *
 * В обоих случаях данные текут отсюда к in_stream. Меняется только то, кто
 * кому позвонил. Протокол и роли в рукопожатии — plugins/include/stream.h.
 *
 *   [OUTPUT]                        [OUTPUT]
 *       Name    stream                  Name    stream
 *       Match   *                       Match   *
 *       Listen  0.0.0.0                 Host    central
 *       Port    10000                   Port    10000
 *       Token   секрет                  Token   секрет
 *
 * Главное свойство от Link не зависит: пока соединения нет, плагин отвечает
 * конвейеру FLB_RETRY. Значит копит не он, а сам fluent-bit своим штатным
 * движком чанков — при storage.type filesystem это диск. Своего размера
 * буфера у выхода поэтому нет и не нужно: им управляют storage.max_chunks_up
 * и storage.pause_on_chunks_overlimit на входе.
 *
 * Потоков два. Свой поток плагина устанавливает соединение (принимает или
 * подключается — по Link), проходит рукопожатие и читает встречные строки
 * (ACK, PONG). cb_flush работает в потоке движка и только пишет пачки. Всё,
 * что касается сокета, идёт под io_lock: OpenSSL допускает одновременные
 * чтение и запись не при всяком раскладе, а TLS 1.3 с обновлением ключей —
 * как раз неудачный расклад.
 *
 * Загрузчик ищет в .so структуру `out_stream_plugin`.
 */

#include <fluent-bit/flb_output_plugin.h>
#include <fluent-bit/flb_config_map.h>
#include <fluent-bit/flb_sds.h>

#include "dbsink.h"
#include "stream.h"

#include <pthread.h>
#include <time.h>

#define DEFAULT_PORT 10000

struct flb_out_stream {
    /* настройки */
    flb_sds_t listen_conf;
    int link;
    flb_sds_t token;
    flb_sds_t node;
    int require_ack;
    int ack_timeout_ms;
    int heartbeat_sec;
    int handshake_ms;
    int retry_pause_sec;
    int secure;
    flb_sds_t cert_file;
    flb_sds_t key_file;
    flb_sds_t ca_file;
    flb_sds_t servername;
    int verify;
    char *time_key;
    char *time_format_conf;
    int time_format;

    /* сеть */
    char host[256];
    int port;
    int listen_fd;                  /* только при Link listen */
    struct stream_io io;            /* установленное соединение, fd < 0 — нет */
    SSL_CTX *ssl_ctx;
    char peer[64];
    time_t next_try;                /* только при Link connect */

    /* поток соединения */
    pthread_t thread;
    int thread_up;
    int stop;

    /* io_lock — на любые операции с сокетом; send_lock — чтобы две пачки не
     * перемешались, когда у выхода несколько workers */
    pthread_mutex_t io_lock;
    pthread_mutex_t send_lock;
    pthread_mutex_t ack_lock;
    pthread_cond_t ack_cv;

    uint64_t seq;                   /* номер последней отправленной пачки */
    uint64_t acked;                 /* номер последней подтверждённой */
    int generation;                 /* растёт на каждом новом соединении */

    uint64_t sent_batches;
    uint64_t sent_records;

    struct flb_output_instance *ins;
};

/* Бросить текущее соединение. Вызывается из обоих потоков, поэтому io_lock
 * берётся снаружи. Номер поколения растёт: ждущий ACK поймёт, что ждать
 * больше нечего. */
static void link_drop_locked(struct flb_out_stream *ctx, const char *why)
{
    if (!stream_io_open(&ctx->io)) {
        return;
    }
    flb_plg_info(ctx->ins, "соединение с %s закрыто: %s", ctx->peer, why);
    stream_io_close(&ctx->io);
    ctx->peer[0] = '\0';
    ctx->next_try = time(NULL) + ctx->retry_pause_sec;

    pthread_mutex_lock(&ctx->ack_lock);
    ctx->generation++;
    pthread_cond_broadcast(&ctx->ack_cv);
    pthread_mutex_unlock(&ctx->ack_lock);
}

/* ------------------------------------------------ установка соединения --- */

/* Link listen: принимаем того, кто пришёл, и спрашиваем у него пароль. */
static void link_accept(struct flb_out_stream *ctx)
{
    struct stream_io io;
    char err[256];
    char node[256];
    char reason[256];
    int fd;

    if (stream_wait_readable(ctx->listen_fd, 200) <= 0) {
        return;
    }
    fd = accept(ctx->listen_fd, NULL, NULL);
    if (fd < 0) {
        return;
    }
    stream_set_timeout(fd, ctx->handshake_ms);
    stream_set_keepalive(fd);
    stream_set_nodelay(fd);

    stream_io_reset(&io);
    io.fd = fd;

    if (ctx->secure) {
        io.ssl = SSL_new(ctx->ssl_ctx);
        if (!io.ssl || SSL_set_fd(io.ssl, fd) != 1 || SSL_accept(io.ssl) != 1) {
            stream_ssl_error(err, sizeof(err));
            flb_plg_warn(ctx->ins, "TLS: рукопожатие не удалось: %s", err);
            stream_io_close(&io);
            return;
        }
    }

    node[0] = '\0';
    if (stream_hello_check(&io, ctx->token, ctx->handshake_ms,
                           node, sizeof(node), reason, sizeof(reason)) != 0) {
        flb_plg_warn(ctx->ins, "рукопожатие отклонено: %s", reason);
        stream_io_close(&io);
        return;
    }

    pthread_mutex_lock(&ctx->io_lock);
    ctx->io = io;
    if (strlen(node) > 0) {
        snprintf(ctx->peer, sizeof(ctx->peer), "%s", node);
    }
    else {
        stream_peer_name(fd, ctx->peer, sizeof(ctx->peer));
    }
    pthread_mutex_unlock(&ctx->io_lock);

    flb_plg_info(ctx->ins, "получатель %s подключился%s", ctx->peer,
                 ctx->secure ? " (TLS)" : "");
}

/* Link connect: звоним сами и представляемся. */
static void link_dial(struct flb_out_stream *ctx)
{
    struct stream_io io;
    char err[256];
    char reason[256];
    int fd;

    if (time(NULL) < ctx->next_try) {
        return;
    }
    ctx->next_try = time(NULL) + ctx->retry_pause_sec;

    fd = stream_connect(ctx->host, ctx->port, ctx->handshake_ms);
    if (fd < 0) {
        flb_plg_debug(ctx->ins, "%s:%i недоступен", ctx->host, ctx->port);
        return;
    }

    stream_io_reset(&io);
    io.fd = fd;

    if (ctx->secure) {
        io.ssl = SSL_new(ctx->ssl_ctx);
        if (!io.ssl || SSL_set_fd(io.ssl, fd) != 1) {
            stream_io_close(&io);
            return;
        }
        SSL_set_tlsext_host_name(io.ssl,
            (ctx->servername && flb_sds_len(ctx->servername) > 0) ?
            ctx->servername : ctx->host);
        if (SSL_connect(io.ssl) != 1) {
            stream_ssl_error(err, sizeof(err));
            flb_plg_warn(ctx->ins, "TLS к %s:%i не установлен: %s",
                         ctx->host, ctx->port, err);
            stream_io_close(&io);
            return;
        }
        if (ctx->verify && SSL_get_verify_result(io.ssl) != X509_V_OK) {
            flb_plg_warn(ctx->ins, "сертификат %s:%i не проверен",
                         ctx->host, ctx->port);
            stream_io_close(&io);
            return;
        }
    }

    if (stream_hello_send(&io, ctx->token, ctx->node, ctx->handshake_ms,
                          reason, sizeof(reason)) != 0) {
        flb_plg_warn(ctx->ins, "%s:%i отказал: %s", ctx->host, ctx->port,
                     reason);
        stream_io_close(&io);
        return;
    }

    pthread_mutex_lock(&ctx->io_lock);
    ctx->io = io;
    snprintf(ctx->peer, sizeof(ctx->peer), "%s:%i", ctx->host, ctx->port);
    pthread_mutex_unlock(&ctx->io_lock);

    flb_plg_info(ctx->ins, "подключился к получателю %s%s", ctx->peer,
                 ctx->secure ? " (TLS)" : "");
}

/* Пока идёт отдача, к порту всё равно стучатся: второй получатель,
 * перезапущенный первый, сканер. Принять и сразу закрыть — единственное, что
 * здесь можно себе позволить: разбирать рукопожатие некогда, этот же поток в
 * это время читает подтверждения, а рукопожатие ждёт до Handshake_Ms.
 *
 * Без этого соединения копятся в очереди прослушивания непринятыми, и
 * подключившийся не получает ни отказа, ни разрыва — просто висит до своего
 * таймаута. */
static void reject_pending(struct flb_out_stream *ctx)
{
    int fd;

    if (stream_wait_readable(ctx->listen_fd, 0) <= 0) {
        return;
    }
    fd = accept(ctx->listen_fd, NULL, NULL);
    if (fd < 0) {
        return;
    }
    close(fd);
    flb_plg_warn(ctx->ins, "подключение отклонено: уже отдаю %s", ctx->peer);
}

/* Разбор встречных строк: подтверждения и ответ на heartbeat. */
static void handle_line(struct flb_out_stream *ctx, const char *line)
{
    unsigned long long n;

    if (strncmp(line, "ACK ", 4) == 0) {
        n = strtoull(line + 4, NULL, 10);
        pthread_mutex_lock(&ctx->ack_lock);
        if ((uint64_t) n > ctx->acked) {
            ctx->acked = (uint64_t) n;
        }
        pthread_cond_broadcast(&ctx->ack_cv);
        pthread_mutex_unlock(&ctx->ack_lock);
        return;
    }
    if (strcmp(line, "PONG") == 0) {
        return;
    }
    if (strcmp(line, "PING") == 0) {
        pthread_mutex_lock(&ctx->io_lock);
        stream_printf(&ctx->io, "PONG\n");
        pthread_mutex_unlock(&ctx->io_lock);
        return;
    }
    flb_plg_debug(ctx->ins, "непонятная строка от получателя: %s", line);
}

static void *stream_thread(void *arg)
{
    struct flb_out_stream *ctx = arg;
    char buf[4096];
    char line[STREAM_LINE_MAX];
    size_t line_used = 0;
    time_t last_seen;
    time_t now;
    int ret;
    int i;
    int fd;

    last_seen = time(NULL);

    while (!ctx->stop) {
        pthread_mutex_lock(&ctx->io_lock);
        fd = ctx->io.fd;
        pthread_mutex_unlock(&ctx->io_lock);

        if (fd < 0) {
            line_used = 0;
            if (ctx->link == STREAM_LINK_LISTEN) {
                link_accept(ctx);
            }
            else {
                link_dial(ctx);
                if (ctx->io.fd < 0) {
                    /* пауза между попытками выдержана в link_dial, здесь
                     * просто не крутим цикл вхолостую; шаг короткий, чтобы
                     * остановка плагина не ждала */
                    usleep(200000);
                }
            }
            last_seen = time(NULL);
            continue;
        }

        if (ctx->link == STREAM_LINK_LISTEN) {
            reject_pending(ctx);
        }

        ret = stream_wait_readable(fd, 200);
        if (ret < 0) {
            pthread_mutex_lock(&ctx->io_lock);
            link_drop_locked(ctx, "сокет отвалился");
            pthread_mutex_unlock(&ctx->io_lock);
            continue;
        }

        if (ret > 0) {
            pthread_mutex_lock(&ctx->io_lock);
            ret = stream_io_open(&ctx->io) ?
                  stream_read_some(&ctx->io, buf, sizeof(buf)) : -1;
            pthread_mutex_unlock(&ctx->io_lock);

            if (ret < 0) {
                pthread_mutex_lock(&ctx->io_lock);
                link_drop_locked(ctx, "соединение закрыто");
                pthread_mutex_unlock(&ctx->io_lock);
                continue;
            }
            if (ret > 0) {
                last_seen = time(NULL);
                for (i = 0; i < ret; i++) {
                    if (buf[i] == '\n') {
                        line[line_used] = '\0';
                        if (line_used > 0) {
                            handle_line(ctx, line);
                        }
                        line_used = 0;
                    }
                    else if (buf[i] != '\r' && line_used + 1 < sizeof(line)) {
                        line[line_used++] = buf[i];
                    }
                }
            }
        }

        /* heartbeat: тишина в обе стороны не отличима от мёртвого узла, а
         * TCP сам по себе про разрыв может молчать часами */
        now = time(NULL);
        if (ctx->heartbeat_sec > 0 && now - last_seen >= ctx->heartbeat_sec) {
            pthread_mutex_lock(&ctx->io_lock);
            if (stream_io_open(&ctx->io) &&
                stream_printf(&ctx->io, "PING\n") != 0) {
                link_drop_locked(ctx, "PING не ушёл");
            }
            pthread_mutex_unlock(&ctx->io_lock);
            last_seen = now;
        }
    }

    pthread_mutex_lock(&ctx->io_lock);
    link_drop_locked(ctx, "плагин останавливается");
    pthread_mutex_unlock(&ctx->io_lock);
    return NULL;
}

/* --------------------------------------------------------------- плагин --- */

static int cb_init(struct flb_output_instance *ins, struct flb_config *config,
                   void *data)
{
    struct flb_out_stream *ctx;
    char err[256];

    (void) data;

    ctx = flb_calloc(1, sizeof(struct flb_out_stream));
    if (!ctx) {
        flb_errno();
        return -1;
    }
    ctx->ins = ins;
    ctx->listen_fd = -1;
    stream_io_reset(&ctx->io);

    if (flb_output_config_map_set(ins, (void *) ctx) == -1) {
        flb_free(ctx);
        return -1;
    }

    ctx->time_format = dbsink_time_format_parse(ctx->time_format_conf);
    if (ctx->time_format < 0) {
        flb_plg_error(ins, "неизвестный Time_Format: %s", ctx->time_format_conf);
        flb_free(ctx);
        return -1;
    }

    /* Режим выводится из конфигурации: задан Listen — слушаем, не задан —
     * подключаемся. Отдельного переключателя нет намеренно: два способа
     * сказать одно и то же расходятся между собой при первой же правке. */
    ctx->port = ins->host.port > 0 ? ins->host.port : DEFAULT_PORT;

    if (ctx->listen_conf && flb_sds_len(ctx->listen_conf) > 0) {
        if (ins->host.name && strlen(ins->host.name) > 0) {
            flb_plg_error(ins, "заданы и Listen, и Host — непонятно, слушать "
                          "или подключаться; оставьте что-то одно");
            flb_free(ctx);
            return -1;
        }
        ctx->link = STREAM_LINK_LISTEN;
        snprintf(ctx->host, sizeof(ctx->host), "%s", ctx->listen_conf);
    }
    else if (ins->host.name && strlen(ins->host.name) > 0) {
        ctx->link = STREAM_LINK_CONNECT;
        snprintf(ctx->host, sizeof(ctx->host), "%s", ins->host.name);
    }
    else {
        flb_plg_error(ins, "укажите Listen (ждать подключения) или Host "
                      "(подключаться самому)");
        flb_free(ctx);
        return -1;
    }

    if (ctx->secure) {
        if (ctx->link == STREAM_LINK_LISTEN) {
            ctx->ssl_ctx = stream_tls_server_ctx(ctx->cert_file, ctx->key_file,
                                                 err, sizeof(err));
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

    pthread_mutex_init(&ctx->io_lock, NULL);
    pthread_mutex_init(&ctx->send_lock, NULL);
    pthread_mutex_init(&ctx->ack_lock, NULL);
    pthread_cond_init(&ctx->ack_cv, NULL);

    if (ctx->link == STREAM_LINK_LISTEN) {
        ctx->listen_fd = stream_listen(ctx->host, ctx->port, 4);
        if (ctx->listen_fd < 0) {
            flb_plg_error(ins, "порт %i не занят под приём: %s", ctx->port,
                          strerror(errno));
            if (ctx->ssl_ctx) {
                SSL_CTX_free(ctx->ssl_ctx);
            }
            flb_free(ctx);
            return -1;
        }
    }

    if (pthread_create(&ctx->thread, NULL, stream_thread, ctx) != 0) {
        flb_plg_error(ins, "поток соединения не запустился");
        if (ctx->listen_fd >= 0) {
            close(ctx->listen_fd);
        }
        if (ctx->ssl_ctx) {
            SSL_CTX_free(ctx->ssl_ctx);
        }
        flb_free(ctx);
        return -1;
    }
    ctx->thread_up = FLB_TRUE;

    flb_output_set_context(ins, ctx);

    flb_plg_info(ins, "%s %s:%i%s%s; подтверждение %s",
                 ctx->link == STREAM_LINK_LISTEN ?
                     "жду получателя на" : "отдаю получателю",
                 ctx->host, ctx->port,
                 ctx->secure ? ", TLS" : "",
                 (ctx->token && flb_sds_len(ctx->token) > 0) ? ", по токену" : "",
                 ctx->require_ack ? "включено" : "выключено");
    return 0;
}

/* Ждём ACK на отправленную пачку. Возвращает 0 — подтверждена, -1 — нет
 * (таймаут или соединение сменилось). */
static int wait_ack(struct flb_out_stream *ctx, uint64_t seq, int generation)
{
    struct timespec deadline;
    int ret = 0;

    clock_gettime(CLOCK_REALTIME, &deadline);
    deadline.tv_sec += ctx->ack_timeout_ms / 1000;
    deadline.tv_nsec += (long) (ctx->ack_timeout_ms % 1000) * 1000000L;
    if (deadline.tv_nsec >= 1000000000L) {
        deadline.tv_sec += 1;
        deadline.tv_nsec -= 1000000000L;
    }

    pthread_mutex_lock(&ctx->ack_lock);
    while (ctx->acked < seq && ctx->generation == generation && ret == 0) {
        ret = pthread_cond_timedwait(&ctx->ack_cv, &ctx->ack_lock, &deadline);
    }
    ret = (ctx->acked >= seq) ? 0 : -1;
    pthread_mutex_unlock(&ctx->ack_lock);

    return ret;
}

static void cb_flush(struct flb_event_chunk *event_chunk,
                     struct flb_output_flush *out_flush,
                     struct flb_input_instance *i_ins,
                     void *out_context, struct flb_config *config)
{
    struct flb_out_stream *ctx = out_context;
    flb_sds_t batch;
    uint64_t seq;
    int generation;
    int rows;
    int ok;

    (void) i_ins;
    (void) config;

    pthread_mutex_lock(&ctx->io_lock);
    ok = stream_io_open(&ctx->io);
    pthread_mutex_unlock(&ctx->io_lock);

    /* Соединения нет — не наше дело копить: возвращаем RETRY, и пачка
     * остаётся чанком fluent-bit (при storage.type filesystem — на диске). */
    if (!ok) {
        FLB_OUTPUT_RETURN(FLB_RETRY);
    }

    batch = flb_sds_create_size(4096);
    if (!batch) {
        FLB_OUTPUT_RETURN(FLB_RETRY);
    }

    rows = dbsink_chunk_json(event_chunk, ctx->time_key, ctx->time_format,
                             "\n", &batch);
    if (rows < 0) {
        flb_plg_error(ctx->ins, "пачка событий не разобрана");
        flb_sds_destroy(batch);
        FLB_OUTPUT_RETURN(FLB_ERROR);
    }
    if (rows == 0) {
        flb_sds_destroy(batch);
        FLB_OUTPUT_RETURN(FLB_OK);
    }

    pthread_mutex_lock(&ctx->send_lock);

    pthread_mutex_lock(&ctx->io_lock);
    if (!stream_io_open(&ctx->io)) {
        pthread_mutex_unlock(&ctx->io_lock);
        pthread_mutex_unlock(&ctx->send_lock);
        flb_sds_destroy(batch);
        FLB_OUTPUT_RETURN(FLB_RETRY);
    }

    pthread_mutex_lock(&ctx->ack_lock);
    seq = ++ctx->seq;
    generation = ctx->generation;
    pthread_mutex_unlock(&ctx->ack_lock);

    ok = FLB_TRUE;
    if (stream_printf(&ctx->io, "BATCH %llu %zu\n",
                      (unsigned long long) seq, flb_sds_len(batch)) != 0 ||
        stream_write_all(&ctx->io, batch, flb_sds_len(batch)) != 0) {
        link_drop_locked(ctx, "пачка не ушла");
        ok = FLB_FALSE;
    }
    pthread_mutex_unlock(&ctx->io_lock);

    flb_sds_destroy(batch);

    if (!ok) {
        pthread_mutex_unlock(&ctx->send_lock);
        FLB_OUTPUT_RETURN(FLB_RETRY);
    }

    if (ctx->require_ack && wait_ack(ctx, seq, generation) != 0) {
        pthread_mutex_lock(&ctx->io_lock);
        link_drop_locked(ctx, "подтверждение не пришло");
        pthread_mutex_unlock(&ctx->io_lock);
        pthread_mutex_unlock(&ctx->send_lock);
        flb_plg_warn(ctx->ins, "пачка %llu не подтверждена, уйдёт повтором",
                     (unsigned long long) seq);
        FLB_OUTPUT_RETURN(FLB_RETRY);
    }

    ctx->sent_batches++;
    ctx->sent_records += (uint64_t) rows;
    pthread_mutex_unlock(&ctx->send_lock);

    flb_plg_debug(ctx->ins, "пачка %llu: записей %i, всего %llu",
                  (unsigned long long) seq, rows,
                  (unsigned long long) ctx->sent_records);

    FLB_OUTPUT_RETURN(FLB_OK);
}

static int cb_exit(void *data, struct flb_config *config)
{
    struct flb_out_stream *ctx = data;

    (void) config;

    if (!ctx) {
        return 0;
    }
    ctx->stop = FLB_TRUE;
    if (ctx->thread_up) {
        pthread_join(ctx->thread, NULL);
    }
    if (ctx->listen_fd >= 0) {
        close(ctx->listen_fd);
    }
    if (ctx->ssl_ctx) {
        SSL_CTX_free(ctx->ssl_ctx);
    }
    pthread_cond_destroy(&ctx->ack_cv);
    pthread_mutex_destroy(&ctx->ack_lock);
    pthread_mutex_destroy(&ctx->send_lock);
    pthread_mutex_destroy(&ctx->io_lock);
    flb_free(ctx);
    return 0;
}

static struct flb_config_map config_map[] = {
    {
     FLB_CONFIG_MAP_STR, "listen", NULL,
     0, FLB_TRUE, offsetof(struct flb_out_stream, listen_conf),
     "Адрес прослушивания. Задан — ждём, когда придут за логами; не задан — "
     "подключаемся сами к Host:Port"
    },
    {
     FLB_CONFIG_MAP_STR, "token", NULL,
     0, FLB_TRUE, offsetof(struct flb_out_stream, token),
     "Пароль: у пришедших спрашиваем, при своём подключении предъявляем"
    },
    {
     FLB_CONFIG_MAP_STR, "node", NULL,
     0, FLB_TRUE, offsetof(struct flb_out_stream, node),
     "Имя, которым представляемся, когда подключаемся сами: станет меткой источника"
    },
    {
     FLB_CONFIG_MAP_BOOL, "require_ack", "true",
     0, FLB_TRUE, offsetof(struct flb_out_stream, require_ack),
     "Ждать подтверждения пачки; без него потеря при обрыве не заметна"
    },
    {
     FLB_CONFIG_MAP_INT, "ack_timeout_ms", "10000",
     0, FLB_TRUE, offsetof(struct flb_out_stream, ack_timeout_ms),
     "Сколько ждать подтверждения, прежде чем считать соединение мёртвым"
    },
    {
     FLB_CONFIG_MAP_INT, "heartbeat_sec", "30",
     0, FLB_TRUE, offsetof(struct flb_out_stream, heartbeat_sec),
     "Период PING при простое; 0 — не слать"
    },
    {
     FLB_CONFIG_MAP_INT, "handshake_ms", "5000",
     0, FLB_TRUE, offsetof(struct flb_out_stream, handshake_ms),
     "Таймаут рукопожатия, а когда звоним сами — и подключения"
    },
    {
     FLB_CONFIG_MAP_INT, "retry_pause_sec", "10",
     0, FLB_TRUE, offsetof(struct flb_out_stream, retry_pause_sec),
     "Пауза перед повторным подключением, когда подключаемся сами"
    },
    {
     FLB_CONFIG_MAP_BOOL, "secure", "false",
     0, FLB_TRUE, offsetof(struct flb_out_stream, secure),
     "TLS: когда слушаем — свой сертификат, когда звоним — проверка чужого"
    },
    {
     FLB_CONFIG_MAP_STR, "cert_file", NULL,
     0, FLB_TRUE, offsetof(struct flb_out_stream, cert_file),
     "Свой сертификат в PEM (нужен, когда слушаем)"
    },
    {
     FLB_CONFIG_MAP_STR, "key_file", NULL,
     0, FLB_TRUE, offsetof(struct flb_out_stream, key_file),
     "Свой закрытый ключ в PEM (нужен, когда слушаем)"
    },
    {
     FLB_CONFIG_MAP_STR, "ca_file", NULL,
     0, FLB_TRUE, offsetof(struct flb_out_stream, ca_file),
     "Корневой сертификат для проверки той стороны (когда звоним сами)"
    },
    {
     FLB_CONFIG_MAP_BOOL, "verify", "true",
     0, FLB_TRUE, offsetof(struct flb_out_stream, verify),
     "Считать непроверенный сертификат ошибкой (когда звоним сами)"
    },
    {
     FLB_CONFIG_MAP_STR, "servername", NULL,
     0, FLB_TRUE, offsetof(struct flb_out_stream, servername),
     "Имя для SNI, если отличается от адреса"
    },
    {
     FLB_CONFIG_MAP_STR, "time_key", "timestamp",
     0, FLB_TRUE, offsetof(struct flb_out_stream, time_key),
     "Поле, куда положить время события; пустое — не добавлять"
    },
    {
     FLB_CONFIG_MAP_STR, "time_format", "iso8601",
     0, FLB_TRUE, offsetof(struct flb_out_stream, time_format_conf),
     "Формат времени: iso8601, datetime64 или epoch"
    },
    {0}
};

struct flb_output_plugin out_stream_plugin = {
    .name         = "stream",
    .description  = "Отдаёт записи; Listen — ждём получателя, Host — звоним сами",
    .cb_init      = cb_init,
    .cb_flush     = cb_flush,
    .cb_exit      = cb_exit,
    .config_map   = config_map,
    .event_type   = FLB_OUTPUT_LOGS | FLB_OUTPUT_METRICS,
    .flags        = FLB_OUTPUT_NET,
};
