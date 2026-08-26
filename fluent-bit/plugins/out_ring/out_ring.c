/* out_ring — кольцевой буфер с выдачей по запросу.
 *
 * Плагин ничего никуда не отправляет: события копятся в кольце фиксированного
 * размера, а забирает их потребитель сам — обычным GET. Кольцо пишется по
 * кругу: когда набранное превышает Ring_Size, вытесняется самое старое.
 *
 *   GET    <Uri>          отдать всё, что накопилось, ничего не трогая
 *   GET    <Clear_Uri>    отдать и очистить — но только если отдача удалась
 *   DELETE <Uri>          то же самое, если удобнее методом
 *   GET    <Stats_Uri>    сколько лежит, сколько вытеснено (JSON)
 *
 * Почему очистка отдельным путём, а не ?clear=1: у HTTP/1 в 5.x обработчик
 * получает путь уже без строки запроса — monkey отделяет её на разборе, и к
 * моменту вызова плагина её буфер переиспользован (проверено отладочной
 * печатью: query_string, uri и uri_processed пусты). Параметр ?clear=1
 * поддержан там, где он до плагина всё-таки доходит.
 *
 * Формат выдачи — JSON-строки, по событию на строку. Резать ответ по '\n'
 * безопасно: переводы строк внутри значений экранированы (см. dbsink.h).
 *
 * Про блокировку: cb_flush работает в потоке выхода (а с workers их
 * несколько), обработчик HTTP — в своём. Запросов может прийти несколько
 * разом, поэтому кольцо живёт под мьютексом, и он держится на всё время
 * «собрать ответ → отдать → очистить»: иначе два одновременных запроса с
 * очисткой получили бы одни и те же записи дважды.
 *
 * Про гарантии: плагин отвечает конвейеру FLB_OK сразу, как положил записи в
 * кольцо. Значит штатные Retry_Limit и дисковый буфер эти данные больше не
 * страхуют — кольцо и есть буфер, и оно по определению теряет старое.
 *
 * Загрузчик ищет в .so структуру `out_ring_plugin`.
 */

#include <fluent-bit/flb_output_plugin.h>
#include <fluent-bit/flb_config_map.h>
#include <fluent-bit/flb_sds.h>
#include <fluent-bit/flb_version.h>
#include <fluent-bit/http_server/flb_hs_utils.h>

#include "dbsink.h"

#include <pthread.h>

/* HTTP-слой у fluent-bit менялся: в 5.x это flb_http_server_* (тот же, что у
 * штатного prometheus_exporter), в 3.2 и 4.x — напрямую встроенный monkey.
 * Оба набора символов экспортированы из бинарника, поэтому плагин собирается
 * на всех четырёх версиях, просто разными половинами файла. */
#if (FLB_VERSION_MAJOR >= 5)
#define RING_HTTP_NEW 1
#include <fluent-bit/http_server/flb_http_server.h>
#else
#define RING_HTTP_NEW 0
#include <monkey/mk_lib.h>
#endif

#define DEFAULT_PORT 2022

/* Одна запись в кольце. Хранится уже готовой строкой JSON: собирать её на
 * выдаче было бы дороже, а память под msgpack всё равно пришлось бы держать. */
struct ring_item {
    flb_sds_t json;
    struct mk_list _head;
};

struct flb_out_ring {
    struct flb_output_instance *ins;

    /* конфигурация */
    flb_sds_t uri;
    flb_sds_t clear_uri_conf;
    flb_sds_t stats_uri;
    size_t ring_size;
    flb_sds_t time_key;
    flb_sds_t time_format_conf;
    int time_format;

    flb_sds_t clear_uri;        /* заданный или собранный <Uri>/clear */

    /* кольцо */
    struct mk_list items;
    size_t bytes;               /* сколько занято сейчас */
    uint64_t records;           /* записей в кольце */
    uint64_t dropped;           /* вытеснено за всё время */
    uint64_t received;          /* принято за всё время */
    uint64_t served;            /* отдано за всё время */
    int serving;                /* выдача уже идёт: второй запрос ждать не будет */
    pthread_mutex_t lock;

#if RING_HTTP_NEW
    struct flb_http_server server;
#else
    mk_ctx_t *mk;
#endif
    int server_up;
};

/* У monkey обработчик получает только свой запрос, контекст передать некуда,
 * поэтому ссылка держится рядом. Инстанс out_ring в конфигурации ожидается
 * один; при втором в лог уйдёт предупреждение. */
#if !RING_HTTP_NEW
static struct flb_out_ring *ring_singleton = NULL;
#endif

/* ------------------------------------------------------------------ кольцо */

static void ring_item_destroy(struct ring_item *item)
{
    mk_list_del(&item->_head);
    flb_sds_destroy(item->json);
    flb_free(item);
}

/* Вытеснение самых старых, пока не влезем в Ring_Size. Под замком. */
static void ring_trim(struct flb_out_ring *ctx)
{
    struct mk_list *head;
    struct mk_list *tmp;
    struct ring_item *item;

    mk_list_foreach_safe(head, tmp, &ctx->items) {
        if (ctx->bytes <= ctx->ring_size) {
            break;
        }
        item = mk_list_entry(head, struct ring_item, _head);
        ctx->bytes -= flb_sds_len(item->json) + 1;
        ctx->records--;
        ctx->dropped++;
        ring_item_destroy(item);
    }
}

/* Под замком. */
static int ring_push(struct flb_out_ring *ctx, const char *json, size_t len)
{
    struct ring_item *item;

    /* запись длиннее всего кольца не поместится никогда — считаем вытесненной
     * сразу, иначе ring_trim выбросил бы ради неё всё остальное */
    if (len + 1 > ctx->ring_size) {
        ctx->dropped++;
        return -1;
    }

    item = flb_calloc(1, sizeof(struct ring_item));
    if (!item) {
        flb_errno();
        return -1;
    }
    item->json = flb_sds_create_len(json, len);
    if (!item->json) {
        flb_free(item);
        return -1;
    }

    mk_list_add(&item->_head, &ctx->items);
    ctx->bytes += len + 1;      /* +1 — перевод строки при выдаче */
    ctx->records++;
    ctx->received++;
    ring_trim(ctx);
    return 0;
}

/* Под замком. */
static void ring_clear(struct flb_out_ring *ctx)
{
    struct mk_list *head;
    struct mk_list *tmp;
    struct ring_item *item;

    mk_list_foreach_safe(head, tmp, &ctx->items) {
        item = mk_list_entry(head, struct ring_item, _head);
        ring_item_destroy(item);
    }
    ctx->bytes = 0;
    ctx->records = 0;
}

/* Под замком. Снять n самых старых — ровно те, что ушли в ответ. Пришедшие
 * за время отправки остаются в кольце. */
static void ring_drop_first(struct flb_out_ring *ctx, uint64_t n)
{
    struct mk_list *head;
    struct mk_list *tmp;
    struct ring_item *item;

    mk_list_foreach_safe(head, tmp, &ctx->items) {
        if (n == 0) {
            break;
        }
        item = mk_list_entry(head, struct ring_item, _head);
        ctx->bytes -= flb_sds_len(item->json) + 1;
        ctx->records--;
        n--;
        ring_item_destroy(item);
    }
}

/* Под замком. Склейка всего кольца в один ответ. */
static flb_sds_t ring_payload(struct flb_out_ring *ctx)
{
    struct mk_list *head;
    struct ring_item *item;
    flb_sds_t out;

    out = flb_sds_create_size(ctx->bytes + 1);
    if (!out) {
        return NULL;
    }
    mk_list_foreach(head, &ctx->items) {
        item = mk_list_entry(head, struct ring_item, _head);
        flb_sds_cat_safe(&out, item->json, flb_sds_len(item->json));
        flb_sds_cat_safe(&out, "\n", 1);
    }
    return out;
}

static flb_sds_t ring_stats(struct flb_out_ring *ctx)
{
    flb_sds_t out;

    out = flb_sds_create_size(256);
    if (!out) {
        return NULL;
    }
    pthread_mutex_lock(&ctx->lock);
    flb_sds_printf(&out,
                   "{\"records\":%" PRIu64 ",\"bytes\":%zu,\"limit\":%zu,"
                   "\"dropped\":%" PRIu64 ",\"received\":%" PRIu64
                   ",\"served\":%" PRIu64 "}\n",
                   ctx->records, ctx->bytes, ctx->ring_size,
                   ctx->dropped, ctx->received, ctx->served);
    pthread_mutex_unlock(&ctx->lock);
    return out;
}

/* "clear=1&x=2" → есть ли clear со значением 1/true/yes. Строка может не
 * оканчиваться нулём (указатель внутрь буфера запроса), поэтому по длине. */
static int query_flag(const char *query, size_t query_len, const char *name)
{
    size_t name_len = strlen(name);
    char buf[256];
    const char *p;
    const char *val;

    if (!query || query_len == 0 || query_len >= sizeof(buf)) {
        return FLB_FALSE;
    }
    memcpy(buf, query, query_len);
    buf[query_len] = '\0';
    p = buf;

    while (*p) {
        if (strncmp(p, name, name_len) == 0 && p[name_len] == '=') {
            val = p + name_len + 1;
            if (*val == '1' ||
                strncasecmp(val, "true", 4) == 0 ||
                strncasecmp(val, "yes", 3) == 0) {
                return FLB_TRUE;
            }
            return FLB_FALSE;
        }
        p = strchr(p, '&');
        if (!p) {
            break;
        }
        p++;
    }
    return FLB_FALSE;
}

static int path_is_clear(struct flb_out_ring *ctx, const char *path)
{
    return ctx->clear_uri && path && strcmp(path, ctx->clear_uri) == 0;
}

/* ------------------------------------------------------- HTTP: 5.x и новее */
#if RING_HTTP_NEW

static int handler_new(struct flb_http_request *request,
                       struct flb_http_response *response)
{
    struct flb_out_ring *ctx;
    flb_sds_t payload;
    uint64_t sent;
    int clear;
    int ret;

    ctx = response->stream->user_data;
    if (ctx == NULL || request->path == NULL) {
        flb_http_response_set_status(response, 500);
        return flb_http_response_commit(response);
    }

    if (ctx->stats_uri && flb_sds_len(ctx->stats_uri) > 0 &&
        strcmp(request->path, ctx->stats_uri) == 0) {
        payload = ring_stats(ctx);
        if (!payload) {
            flb_http_response_set_status(response, 500);
            return flb_http_response_commit(response);
        }
        ret = flb_hs_response_set_payload(response, 200,
                                          FLB_HS_CONTENT_TYPE_JSON,
                                          payload, flb_sds_len(payload));
        flb_sds_destroy(payload);
        return ret;
    }

    if (strcmp(request->path, ctx->uri) != 0 &&
        !path_is_clear(ctx, request->path)) {
        flb_http_response_set_status(response, 404);
        return flb_http_response_commit(response);
    }

    clear = path_is_clear(ctx, request->path) ||
            request->method == HTTP_METHOD_DELETE ||
            (request->query_string != NULL &&
             query_flag(request->query_string,
                        strlen(request->query_string), "clear"));

    /* Замок берётся только на снимок и на снятие отданного, но НЕ на время
     * отправки: иначе застрявший клиент заблокировал бы и приём данных.
     * От двойной выдачи защищает флаг serving — пока идёт одна выдача,
     * остальные запросы получают пусто, а не те же записи повторно. */
    pthread_mutex_lock(&ctx->lock);
    if (ctx->serving || ctx->records == 0) {
        pthread_mutex_unlock(&ctx->lock);
        return flb_hs_response_set_payload(response, 200,
                                           FLB_HS_CONTENT_TYPE_JSON, NULL, 0);
    }
    payload = ring_payload(ctx);
    if (!payload) {
        pthread_mutex_unlock(&ctx->lock);
        flb_http_response_set_status(response, 500);
        return flb_http_response_commit(response);
    }
    sent = ctx->records;
    ctx->serving = FLB_TRUE;
    pthread_mutex_unlock(&ctx->lock);

    ret = flb_hs_response_set_payload(response, 200, FLB_HS_CONTENT_TYPE_JSON,
                                      payload, flb_sds_len(payload));

    pthread_mutex_lock(&ctx->lock);
    ctx->serving = FLB_FALSE;
    /* снимаем только если отдача удалась, и ровно отданное: пришедшее за это
     * время остаётся в кольце */
    if (ret == 0) {
        ctx->served += sent;
        if (clear) {
            ring_drop_first(ctx, sent);
        }
    }
    else {
        flb_plg_warn(ctx->ins, "отдача не удалась, кольцо не очищено");
    }
    pthread_mutex_unlock(&ctx->lock);

    flb_sds_destroy(payload);
    return ret;
}

static int server_create(struct flb_out_ring *ctx, struct flb_config *config)
{
    struct flb_output_instance *ins = ctx->ins;

    /* 12-аргументный init есть во всех четырёх версиях; вариант с options
     * появился только в 5.x, поэтому берём общий */
    return flb_http_server_init(&ctx->server,
                                HTTP_PROTOCOL_VERSION_11,
                                0,
                                handler_new,
                                ins->host.name,
                                ins->host.port,
                                NULL,
                                ins->flags,
                                &ins->net_setup,
                                config->evl,
                                config,
                                ctx);
}

static int server_start(struct flb_out_ring *ctx)
{
    return flb_http_server_start(&ctx->server);
}

static void server_stop(struct flb_out_ring *ctx)
{
    flb_http_server_stop(&ctx->server);
    flb_http_server_destroy(&ctx->server);
}

/* --------------------------------------------------------- HTTP: 3.2 и 4.x */
#else

static void handler_old(mk_request_t *request, void *data)
{
    struct flb_out_ring *ctx = ring_singleton;
    flb_sds_t payload;
    uint64_t sent;
    int clear;
    int ret;

    (void) data;

    if (ctx == NULL) {
        mk_http_status(request, 500);
        mk_http_done(request);
        return;
    }

    /* monkey зовёт обработчик по точному пути; остаётся понять, был ли это
     * путь очистки. Здесь строка запроса разобрана, поэтому ?clear=1 работает */
    clear = (request->uri_processed.data != NULL &&
             request->uri_processed.len == flb_sds_len(ctx->clear_uri) &&
             strncmp(request->uri_processed.data, ctx->clear_uri,
                     request->uri_processed.len) == 0) ||
            request->method == MK_METHOD_DELETE ||
            query_flag(request->query_string.data,
                       request->query_string.len, "clear");

    /* см. комментарий в обработчике 5.x: замок не держим на время отправки,
     * от двойной выдачи защищает serving */
    pthread_mutex_lock(&ctx->lock);
    if (ctx->serving || ctx->records == 0) {
        pthread_mutex_unlock(&ctx->lock);
        mk_http_status(request, 200);
        flb_hs_add_content_type_to_req(request, FLB_HS_CONTENT_TYPE_JSON);
        mk_http_done(request);
        return;
    }
    payload = ring_payload(ctx);
    if (!payload) {
        pthread_mutex_unlock(&ctx->lock);
        mk_http_status(request, 500);
        mk_http_done(request);
        return;
    }
    sent = ctx->records;
    ctx->serving = FLB_TRUE;
    pthread_mutex_unlock(&ctx->lock);

    mk_http_status(request, 200);
    flb_hs_add_content_type_to_req(request, FLB_HS_CONTENT_TYPE_JSON);
    ret = mk_http_send(request, payload, flb_sds_len(payload), NULL);
    mk_http_done(request);

    /* mk_http_send отдаёт не 0/-1, а статус канала: MK_CHANNEL_DONE, _FLUSH,
     * _EMPTY — данные ушли или уйдут; ошибка только _ERROR и отрицательные */
    /* mk_http_send отдаёт статус канала, а не признак доставки: в
     * библиотечном режиме monkey он возвращает MK_CHANNEL_ERROR даже когда
     * клиент данные получил (проверено). Поэтому признаком неудачи считаем
     * только отрицательный код — большего этот слой сказать не может */
    pthread_mutex_lock(&ctx->lock);
    ctx->serving = FLB_FALSE;
    if (ret >= 0) {
        ctx->served += sent;
        if (clear) {
            ring_drop_first(ctx, sent);
        }
    }
    else {
        flb_plg_warn(ctx->ins, "отдача не удалась, кольцо не очищено");
    }
    pthread_mutex_unlock(&ctx->lock);

    flb_sds_destroy(payload);
}

static void handler_old_stats(mk_request_t *request, void *data)
{
    struct flb_out_ring *ctx = ring_singleton;
    flb_sds_t payload;

    (void) data;

    if (ctx == NULL) {
        mk_http_status(request, 500);
        mk_http_done(request);
        return;
    }
    payload = ring_stats(ctx);
    if (!payload) {
        mk_http_status(request, 500);
        mk_http_done(request);
        return;
    }
    mk_http_status(request, 200);
    flb_hs_add_content_type_to_req(request, FLB_HS_CONTENT_TYPE_JSON);
    mk_http_send(request, payload, flb_sds_len(payload), NULL);
    mk_http_done(request);
    flb_sds_destroy(payload);
}

static int server_create(struct flb_out_ring *ctx, struct flb_config *config)
{
    struct flb_output_instance *ins = ctx->ins;
    char listen[64];
    int vid;

    (void) config;

    if (ring_singleton != NULL) {
        flb_plg_warn(ctx->ins, "в этой версии fluent-bit поддержан один "
                     "инстанс out_ring, второй перехватит выдачу");
    }

    ctx->mk = mk_create();
    if (!ctx->mk) {
        return -1;
    }

    snprintf(listen, sizeof(listen) - 1, "%s:%d",
             ins->host.name, ins->host.port);
    mk_config_set(ctx->mk, "Listen", listen, "Workers", "1", NULL);

    vid = mk_vhost_create(ctx->mk, NULL);
    mk_vhost_handler(ctx->mk, vid, ctx->uri, handler_old, NULL);
    mk_vhost_handler(ctx->mk, vid, ctx->clear_uri, handler_old, NULL);
    if (ctx->stats_uri && flb_sds_len(ctx->stats_uri) > 0) {
        mk_vhost_handler(ctx->mk, vid, ctx->stats_uri, handler_old_stats, NULL);
    }

    ring_singleton = ctx;
    return 0;
}

static int server_start(struct flb_out_ring *ctx)
{
    return mk_start(ctx->mk);
}

static void server_stop(struct flb_out_ring *ctx)
{
    mk_stop(ctx->mk);
    mk_destroy(ctx->mk);
    ring_singleton = NULL;
}

#endif

/* ------------------------------------------------------------------ выход */

static int cb_init(struct flb_output_instance *ins, struct flb_config *config,
                   void *data)
{
    struct flb_out_ring *ctx;
    int ret;

    (void) data;

    flb_output_net_default("0.0.0.0", DEFAULT_PORT, ins);

    ctx = flb_calloc(1, sizeof(struct flb_out_ring));
    if (!ctx) {
        flb_errno();
        return -1;
    }
    ctx->ins = ins;
    mk_list_init(&ctx->items);
    pthread_mutex_init(&ctx->lock, NULL);
    flb_output_set_context(ins, ctx);

    ret = flb_output_config_map_set(ins, (void *) ctx);
    if (ret == -1) {
        return -1;
    }

    ctx->time_format = dbsink_time_format_parse(ctx->time_format_conf);
    if (ctx->time_format < 0) {
        flb_plg_error(ins, "time_format: datetime64, iso8601 или epoch (задано %s)",
                      ctx->time_format_conf);
        return -1;
    }
    if (!ctx->uri || flb_sds_len(ctx->uri) == 0 || ctx->uri[0] != '/') {
        flb_plg_error(ins, "uri должен начинаться со слэша");
        return -1;
    }
    if (ctx->ring_size == 0) {
        flb_plg_error(ins, "ring_size не может быть нулевым");
        return -1;
    }

    if (ctx->clear_uri_conf && flb_sds_len(ctx->clear_uri_conf) > 0) {
        ctx->clear_uri = flb_sds_create(ctx->clear_uri_conf);
    }
    else {
        ctx->clear_uri = flb_sds_create_size(flb_sds_len(ctx->uri) + 8);
        if (ctx->clear_uri) {
            flb_sds_printf(&ctx->clear_uri, "%s/clear", ctx->uri);
        }
    }
    if (!ctx->clear_uri) {
        return -1;
    }

    if (server_create(ctx, config) != 0) {
        flb_plg_error(ins, "HTTP-сервер не поднялся");
        return -1;
    }
    if (server_start(ctx) != 0) {
        flb_plg_error(ins, "HTTP-сервер не запустился на %s:%i",
                      ins->host.name, ins->host.port);
        return -1;
    }
    ctx->server_up = FLB_TRUE;

    flb_plg_info(ins, "кольцо %zu байт на %s:%i; GET %s — отдать, "
                 "GET %s или DELETE %s — отдать и очистить",
                 ctx->ring_size, ins->host.name, ins->host.port,
                 ctx->uri, ctx->clear_uri, ctx->uri);
    return 0;
}

static void cb_flush(struct flb_event_chunk *event_chunk,
                     struct flb_output_flush *out_flush,
                     struct flb_input_instance *i_ins,
                     void *out_context, struct flb_config *config)
{
    struct flb_out_ring *ctx = out_context;
    flb_sds_t batch;
    char *line;
    char *nl;
    size_t len;
    int rows;

    (void) i_ins;
    (void) config;

    batch = flb_sds_create_size(4096);
    if (!batch) {
        FLB_OUTPUT_RETURN(FLB_RETRY);
    }

    /* и записи, и метрики: dbsink разложит одинаково, по строке на событие */
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

    /* режем по переводам строк: внутри значений они экранированы, поэтому
     * граница строки всегда совпадает с границей события */
    pthread_mutex_lock(&ctx->lock);
    line = batch;
    while (line < batch + flb_sds_len(batch)) {
        nl = memchr(line, '\n', (batch + flb_sds_len(batch)) - line);
        len = nl ? (size_t) (nl - line) : (size_t) ((batch + flb_sds_len(batch)) - line);
        if (len > 0) {
            ring_push(ctx, line, len);
        }
        if (!nl) {
            break;
        }
        line = nl + 1;
    }
    pthread_mutex_unlock(&ctx->lock);

    flb_sds_destroy(batch);

    /* конвейеру отвечаем сразу: дальше за сохранность отвечает кольцо */
    FLB_OUTPUT_RETURN(FLB_OK);
}

static int cb_exit(void *data, struct flb_config *config)
{
    struct flb_out_ring *ctx = data;

    (void) config;

    if (!ctx) {
        return 0;
    }
    if (ctx->server_up) {
        server_stop(ctx);
    }
    pthread_mutex_lock(&ctx->lock);
    ring_clear(ctx);
    pthread_mutex_unlock(&ctx->lock);
    pthread_mutex_destroy(&ctx->lock);
    flb_sds_destroy(ctx->clear_uri);
    flb_free(ctx);
    return 0;
}

static struct flb_config_map config_map[] = {
    {
     FLB_CONFIG_MAP_STR, "uri", "/logs",
     0, FLB_TRUE, offsetof(struct flb_out_ring, uri),
     "Путь, по которому отдаётся содержимое кольца"
    },
    {
     FLB_CONFIG_MAP_STR, "clear_uri", NULL,
     0, FLB_TRUE, offsetof(struct flb_out_ring, clear_uri_conf),
     "Путь «отдать и очистить»; по умолчанию <Uri>/clear"
    },
    {
     FLB_CONFIG_MAP_STR, "stats_uri", "/stats",
     0, FLB_TRUE, offsetof(struct flb_out_ring, stats_uri),
     "Путь со счётчиками кольца; пустой — не отдавать"
    },
    {
     FLB_CONFIG_MAP_SIZE, "ring_size", "64M",
     0, FLB_TRUE, offsetof(struct flb_out_ring, ring_size),
     "Размер кольца в байтах; при переполнении вытесняется самое старое"
    },
    {
     FLB_CONFIG_MAP_STR, "time_key", "timestamp",
     0, FLB_TRUE, offsetof(struct flb_out_ring, time_key),
     "Поле, куда положить время события; пустое — не добавлять"
    },
    {
     FLB_CONFIG_MAP_STR, "time_format", "iso8601",
     0, FLB_TRUE, offsetof(struct flb_out_ring, time_format_conf),
     "Формат времени: iso8601, datetime64 или epoch"
    },
    {0}
};

struct flb_output_plugin out_ring_plugin = {
    .name         = "ring",
    .description  = "Кольцевой буфер в памяти с выдачей по HTTP GET",
    .cb_init      = cb_init,
    .cb_flush     = cb_flush,
    .cb_exit      = cb_exit,
    .config_map   = config_map,
    .event_type   = FLB_OUTPUT_LOGS | FLB_OUTPUT_METRICS,
    .flags        = FLB_OUTPUT_NET,
};
