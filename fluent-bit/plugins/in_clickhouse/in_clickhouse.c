/* in_clickhouse — входной плагин: выполняет SQL в ClickHouse по HTTP, строки
 * ответа (JSONEachRow) становятся записями (Mode logs) или метриками
 * (Mode metrics).
 *
 * Загрузчик (src/flb_plugin.c) ищет в .so структуру `in_clickhouse_plugin`.
 * {CURSOR} — инкрементальное чтение журналов, с cursor_file переживает перезапуск.
 */

#include <fluent-bit/flb_input_plugin.h>
#include <fluent-bit/flb_http_client.h>
#include <fluent-bit/flb_upstream.h>
#include <fluent-bit/flb_pack.h>
#include <fluent-bit/flb_time.h>
#include <fluent-bit/flb_config_map.h>
#include <fluent-bit/flb_sds.h>
#include <fluent-bit/flb_log_event_encoder.h>
#include <fluent-bit/tls/flb_tls.h>

#include "dbmetrics.h"

#include <inttypes.h>

#define DEFAULT_INTERVAL_SEC  "5"
#define DEFAULT_PORT          8123
#define DEFAULT_PORT_TLS      8443
#define CURSOR_MAX            128

/* Одна нода: соединение и её собственный курсор. */
struct ch_target {
    flb_sds_t host;
    int port;
    struct flb_upstream *u;
    char cursor[CURSOR_MAX];
    struct mk_list _head;
};

struct flb_in_clickhouse {
    struct flb_input_instance *ins;
    struct mk_list targets;

    /* конфигурация */
    flb_sds_t targets_conf;
    flb_sds_t query;
    flb_sds_t query_file;
    flb_sds_t user;
    flb_sds_t password;
    flb_sds_t database;
    flb_sds_t instance_field;
    flb_sds_t time_field;
    flb_sds_t cursor_field;
    flb_sds_t cursor_default;
    flb_sds_t cursor_file;
    flb_sds_t cursor_type_conf;
    flb_sds_t mode_conf;
    flb_sds_t metric_prefix;
    flb_sds_t metrics_tag;
    flb_sds_t label_fields;
    flb_sds_t value_fields;
    size_t buffer_max_size;
    int interval_sec;
    int interval_nsec;

    flb_sds_t sql;          /* текст запроса, прочитанный один раз */
    int mode;               /* DBM_MODE_* */
    int cursor_type;        /* DBM_CURSOR_* */
    struct dbm_names labels;
    struct dbm_names values;
    int coll_fd;
    struct flb_log_event_encoder log_encoder;
};

/* ------------------------------------------------------------------ помощники */

static flb_sds_t read_file(struct flb_input_instance *ins, const char *path)
{
    FILE *fp;
    long size;
    flb_sds_t out;
    size_t got;

    fp = fopen(path, "rb");
    if (!fp) {
        flb_plg_error(ins, "не читается файл запроса: %s", path);
        return NULL;
    }
    fseek(fp, 0, SEEK_END);
    size = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    if (size <= 0) {
        fclose(fp);
        flb_plg_error(ins, "файл запроса пуст: %s", path);
        return NULL;
    }
    out = flb_sds_create_size((size_t) size + 1);
    if (!out) {
        fclose(fp);
        return NULL;
    }
    got = fread(out, 1, (size_t) size, fp);
    fclose(fp);
    flb_sds_len_set(out, got);
    out[got] = '\0';
    return out;
}

/* Файл курсора ноды: <cursor_file>.<host>-<port>. Отдельный на ноду — курсоры
 * у нод независимы, общий файл затирал бы соседний. */
static void cursor_path(struct flb_in_clickhouse *ctx, struct ch_target *t,
                        char *out, size_t out_size)
{
    snprintf(out, out_size, "%s.%s-%i", ctx->cursor_file, t->host, t->port);
}

static void cursor_load(struct flb_in_clickhouse *ctx, struct ch_target *t)
{
    char path[512];
    FILE *fp;
    size_t got;

    if (!ctx->cursor_file || flb_sds_len(ctx->cursor_file) == 0) {
        return;
    }
    cursor_path(ctx, t, path, sizeof(path));
    fp = fopen(path, "rb");
    if (!fp) {
        return;
    }
    got = fread(t->cursor, 1, CURSOR_MAX - 1, fp);
    fclose(fp);
    t->cursor[got] = '\0';
    while (got > 0 && (t->cursor[got - 1] == '\n' || t->cursor[got - 1] == ' ')) {
        t->cursor[--got] = '\0';
    }
    if (t->cursor[0] != '\0') {
        flb_plg_info(ctx->ins, "%s:%i — курсор с прошлого запуска: %s",
                     t->host, t->port, t->cursor);
    }
}

static void cursor_save(struct flb_in_clickhouse *ctx, struct ch_target *t)
{
    char path[512];
    FILE *fp;

    if (!ctx->cursor_file || flb_sds_len(ctx->cursor_file) == 0) {
        return;
    }
    cursor_path(ctx, t, path, sizeof(path));
    fp = fopen(path, "wb");
    if (!fp) {
        flb_plg_warn(ctx->ins, "курсор не сохранён: %s", path);
        return;
    }
    fwrite(t->cursor, 1, strlen(t->cursor), fp);
    fclose(fp);
}

/* "host:port,host2:port2" → список нод */
static int parse_targets(struct flb_in_clickhouse *ctx, struct flb_config *config)
{
    char *copy;
    char *save = NULL;
    char *item;
    char *colon;
    struct ch_target *t;
    int io_flags;

    /* TLS настраивает ядро: tls/tls.verify/tls.ca_file — его свойства, до
     * config_map плагина они не доходят. Плагину остаётся взять готовый
     * ins->tls и поднять флаг апстрима */
    io_flags = FLB_IO_TCP;
    if (ctx->ins->use_tls) {
        io_flags |= FLB_IO_TLS;
    }

    copy = flb_strdup(ctx->targets_conf);
    if (!copy) {
        return -1;
    }

    item = strtok_r(copy, ", ", &save);
    while (item) {
        t = flb_calloc(1, sizeof(struct ch_target));
        if (!t) {
            flb_free(copy);
            return -1;
        }
        colon = strrchr(item, ':');
        if (colon) {
            *colon = '\0';
            t->port = atoi(colon + 1);
        }
        if (t->port <= 0) {
            t->port = ctx->ins->use_tls ? DEFAULT_PORT_TLS : DEFAULT_PORT;
        }
        t->host = flb_sds_create(item);
        t->u = flb_upstream_create(config, t->host, t->port, io_flags,
                                   ctx->ins->tls);
        if (!t->u) {
            flb_plg_error(ctx->ins, "не создано соединение с %s:%i", t->host, t->port);
            flb_sds_destroy(t->host);
            flb_free(t);
            flb_free(copy);
            return -1;
        }
        flb_stream_disable_async_mode(&t->u->base);
        cursor_load(ctx, t);
        mk_list_add(&t->_head, &ctx->targets);
        flb_plg_info(ctx->ins, "нода %s:%i", t->host, t->port);
        item = strtok_r(NULL, ", ", &save);
    }
    flb_free(copy);

    if (mk_list_size(&ctx->targets) == 0) {
        flb_plg_error(ctx->ins, "в targets нет ни одной ноды");
        return -1;
    }
    return 0;
}

/* Подстановка {CURSOR}: без курсора — значение по умолчанию из конфигурации. */
static flb_sds_t build_query(struct flb_in_clickhouse *ctx, struct ch_target *t)
{
    flb_sds_t out;
    const char *p;
    const char *mark;
    flb_sds_t value;

    mark = strstr(ctx->sql, "{CURSOR}");
    if (!mark) {
        return flb_sds_create_len(ctx->sql, flb_sds_len(ctx->sql));
    }

    if (t->cursor[0] != '\0') {
        value = flb_sds_create_size(CURSOR_MAX + 32);
        if (!value) {
            return NULL;
        }
        /* по умолчанию курсор — время: сравнение DateTime64 со строкой в
         * clickhouse не проходит, нужно явное приведение */
        switch (ctx->cursor_type) {
        case DBM_CURSOR_NUMBER:
        case DBM_CURSOR_RAW:
            flb_sds_printf(&value, "%s", t->cursor);
            break;
        case DBM_CURSOR_STRING:
            flb_sds_printf(&value, "'%s'", t->cursor);
            break;
        default:
            flb_sds_printf(&value, "toDateTime64('%s', 6)", t->cursor);
            break;
        }
    }
    else {
        value = flb_sds_create(ctx->cursor_default);
        if (!value) {
            return NULL;
        }
    }

    out = flb_sds_create_size(flb_sds_len(ctx->sql) + flb_sds_len(value) + 8);
    if (!out) {
        flb_sds_destroy(value);
        return NULL;
    }
    p = ctx->sql;
    while ((mark = strstr(p, "{CURSOR}")) != NULL) {
        flb_sds_cat_safe(&out, p, mark - p);
        flb_sds_cat_safe(&out, value, flb_sds_len(value));
        p = mark + strlen("{CURSOR}");
    }
    flb_sds_cat_safe(&out, p, strlen(p));
    flb_sds_destroy(value);
    return out;
}

/* ------------------------------------------------------------- разбор строки */

static int key_is(msgpack_object *key, const char *name, size_t name_len)
{
    return key->type == MSGPACK_OBJECT_STR &&
           key->via.str.size == name_len &&
           strncmp(key->via.str.ptr, name, name_len) == 0;
}

/* Значение как строка: в JSONEachRow строками приезжает почти всё, включая
 * 64-битные целые (output_format_json_quote_64bit_integers включён по
 * умолчанию) и время. */
static int obj_str(msgpack_object *o, const char **ptr, size_t *len)
{
    if (o->type != MSGPACK_OBJECT_STR) {
        return -1;
    }
    *ptr = o->via.str.ptr;
    *len = o->via.str.size;
    return 0;
}

/* Значение как число. Строку разбираем тоже — иначе половина счётчиков
 * clickhouse в метрики не попадёт. */
static int obj_number(msgpack_object *o, double *out)
{
    switch (o->type) {
    case MSGPACK_OBJECT_POSITIVE_INTEGER:
        *out = (double) o->via.u64;
        return 0;
    case MSGPACK_OBJECT_NEGATIVE_INTEGER:
        *out = (double) o->via.i64;
        return 0;
    case MSGPACK_OBJECT_FLOAT32:
    case MSGPACK_OBJECT_FLOAT64:
        *out = o->via.f64;
        return 0;
    case MSGPACK_OBJECT_BOOLEAN:
        *out = o->via.boolean ? 1 : 0;
        return 0;
    case MSGPACK_OBJECT_STR:
        return dbm_number(o->via.str.ptr, o->via.str.size, out);
    default:
        return -1;
    }
}

/* Запись: тело — разобранный объект как есть. Имя ноды добавляется только
 * если запроса его не отдал: hostName() точнее, чем адрес из targets. */
static int emit_log(struct flb_in_clickhouse *ctx, struct ch_target *t,
                    msgpack_object *root, const char *mp_buf, size_t mp_size,
                    struct flb_time *tm, int has_instance)
{
    msgpack_sbuffer sbuf;
    msgpack_packer pk;
    uint32_t i;
    int ret;

    ret = flb_log_event_encoder_begin_record(&ctx->log_encoder);
    if (ret == FLB_EVENT_ENCODER_SUCCESS) {
        ret = flb_log_event_encoder_set_timestamp(&ctx->log_encoder, tm);
    }
    if (ret != FLB_EVENT_ENCODER_SUCCESS) {
        flb_log_event_encoder_rollback_record(&ctx->log_encoder);
        return -1;
    }

    if (has_instance || !ctx->instance_field ||
        flb_sds_len(ctx->instance_field) == 0) {
        ret = flb_log_event_encoder_set_body_from_raw_msgpack(&ctx->log_encoder,
                                                              (char *) mp_buf,
                                                              mp_size);
    }
    else {
        /* добавить пару в готовый msgpack нельзя — карта пересобирается */
        msgpack_sbuffer_init(&sbuf);
        msgpack_packer_init(&pk, &sbuf, msgpack_sbuffer_write);
        msgpack_pack_map(&pk, root->via.map.size + 1);
        msgpack_pack_str(&pk, flb_sds_len(ctx->instance_field));
        msgpack_pack_str_body(&pk, ctx->instance_field,
                              flb_sds_len(ctx->instance_field));
        msgpack_pack_str(&pk, flb_sds_len(t->host));
        msgpack_pack_str_body(&pk, t->host, flb_sds_len(t->host));
        for (i = 0; i < root->via.map.size; i++) {
            msgpack_pack_object(&pk, root->via.map.ptr[i].key);
            msgpack_pack_object(&pk, root->via.map.ptr[i].val);
        }
        ret = flb_log_event_encoder_set_body_from_raw_msgpack(&ctx->log_encoder,
                                                              sbuf.data,
                                                              sbuf.size);
        msgpack_sbuffer_destroy(&sbuf);
    }

    if (ret == FLB_EVENT_ENCODER_SUCCESS) {
        ret = flb_log_event_encoder_commit_record(&ctx->log_encoder);
    }
    if (ret != FLB_EVENT_ENCODER_SUCCESS) {
        flb_plg_error(ctx->ins, "запись не закодирована: %i", ret);
        flb_log_event_encoder_rollback_record(&ctx->log_encoder);
        return -1;
    }
    return 0;
}

/* Строка ответа → значения gauge. Метки заполняются первым проходом: их
 * значения нужны раньше, чем встретится первый числовой столбец. */
static void emit_metrics(struct flb_in_clickhouse *ctx, struct ch_target *t,
                         msgpack_object *root, struct dbm_set *set,
                         struct flb_time *tm, const char *instance)
{
    uint64_t ts = flb_time_to_nanosec(tm);
    msgpack_object *key;
    msgpack_object *val;
    const char *name;
    size_t name_len;
    const char *str;
    size_t str_len;
    double value;
    uint32_t i;
    int idx;

    dbm_set_row_begin(set, instance ? instance : t->host);

    for (i = 0; i < root->via.map.size; i++) {
        key = &root->via.map.ptr[i].key;
        if (key->type != MSGPACK_OBJECT_STR) {
            continue;
        }
        idx = dbm_names_index(&ctx->labels, key->via.str.ptr, key->via.str.size);
        if (idx >= 0 &&
            obj_str(&root->via.map.ptr[i].val, &str, &str_len) == 0) {
            dbm_set_label(set, idx, str, str_len);
        }
    }

    for (i = 0; i < root->via.map.size; i++) {
        key = &root->via.map.ptr[i].key;
        val = &root->via.map.ptr[i].val;
        if (key->type != MSGPACK_OBJECT_STR) {
            continue;
        }
        name = key->via.str.ptr;
        name_len = key->via.str.size;

        if (dbm_names_has(&ctx->labels, name, name_len)) {
            continue;
        }
        /* явный список важнее автоопределения: без него в метрики уедет
         * любой столбец, который разбирается в число */
        if (ctx->values.count > 0) {
            if (!dbm_names_has(&ctx->values, name, name_len)) {
                continue;
            }
        }
        else {
            if (ctx->time_field && flb_sds_len(ctx->time_field) > 0 &&
                key_is(key, ctx->time_field, flb_sds_len(ctx->time_field))) {
                continue;
            }
            if (ctx->instance_field && flb_sds_len(ctx->instance_field) > 0 &&
                key_is(key, ctx->instance_field, flb_sds_len(ctx->instance_field))) {
                continue;
            }
        }

        if (obj_number(val, &value) != 0) {
            continue;
        }
        dbm_set_value(set, ctx->ins, ctx->metric_prefix, name, name_len,
                      value, ts);
    }
}

/* Одна строка JSONEachRow. newest получает значение курсора, если оно больше
 * уже увиденного в этом ответе. */
static int process_line(struct flb_in_clickhouse *ctx, struct ch_target *t,
                        const char *line, size_t len, struct dbm_set *set,
                        char *newest, size_t newest_size)
{
    char *mp_buf = NULL;
    size_t mp_size = 0;
    size_t off = 0;
    int root_type;
    size_t consumed = 0;
    msgpack_unpacked upk;
    msgpack_object *root;
    msgpack_object *key;
    struct flb_time tm;
    const char *instance = NULL;
    char instance_buf[256];
    const char *str;
    size_t str_len;
    uint32_t i;
    int has_time = 0;
    int has_instance = 0;
    int ret = -1;

    if (flb_pack_json(line, len, &mp_buf, &mp_size, &root_type, &consumed) != 0) {
        flb_plg_warn(ctx->ins, "строка ответа не разобрана как JSON");
        return -1;
    }

    msgpack_unpacked_init(&upk);
    if (msgpack_unpack_next(&upk, mp_buf, mp_size, &off) != MSGPACK_UNPACK_SUCCESS ||
        upk.data.type != MSGPACK_OBJECT_MAP) {
        flb_plg_warn(ctx->ins, "строка ответа — не объект JSON");
        goto done;
    }
    root = &upk.data;

    flb_time_get(&tm);
    for (i = 0; i < root->via.map.size; i++) {
        key = &root->via.map.ptr[i].key;
        if (key->type != MSGPACK_OBJECT_STR) {
            continue;
        }
        if (!has_time && ctx->time_field && flb_sds_len(ctx->time_field) > 0 &&
            key_is(key, ctx->time_field, flb_sds_len(ctx->time_field)) &&
            obj_str(&root->via.map.ptr[i].val, &str, &str_len) == 0) {
            has_time = (dbm_time(str, str_len, &tm) == 0);
        }
        if (!has_instance && ctx->instance_field &&
            flb_sds_len(ctx->instance_field) > 0 &&
            key_is(key, ctx->instance_field, flb_sds_len(ctx->instance_field)) &&
            obj_str(&root->via.map.ptr[i].val, &str, &str_len) == 0) {
            snprintf(instance_buf, sizeof(instance_buf), "%.*s",
                     (int) str_len, str);
            instance = instance_buf;
            has_instance = 1;
        }
        if (newest && ctx->cursor_field && flb_sds_len(ctx->cursor_field) > 0 &&
            key_is(key, ctx->cursor_field, flb_sds_len(ctx->cursor_field))) {
            char value[CURSOR_MAX];
            msgpack_object *v = &root->via.map.ptr[i].val;
            if (obj_str(v, &str, &str_len) == 0) {
                snprintf(value, sizeof(value), "%.*s", (int) str_len, str);
            }
            else if (v->type == MSGPACK_OBJECT_POSITIVE_INTEGER) {
                snprintf(value, sizeof(value), "%" PRIu64, v->via.u64);
            }
            else {
                value[0] = '\0';
            }
            /* строковое сравнение годится и для чисел одинаковой ширины, и
             * для времени в ISO-виде: и то, и другое упорядочено лексически */
            if (value[0] != '\0' &&
                (newest[0] == '\0' || strcmp(value, newest) > 0)) {
                snprintf(newest, newest_size, "%s", value);
            }
        }
    }

    if (ctx->mode != DBM_MODE_METRICS) {
        if (emit_log(ctx, t, root, mp_buf, mp_size, &tm, has_instance) != 0) {
            goto done;
        }
    }
    if (set) {
        emit_metrics(ctx, t, root, set, &tm, instance);
    }
    ret = 0;

done:
    msgpack_unpacked_destroy(&upk);
    flb_free(mp_buf);
    return ret;
}

/* ------------------------------------------------------------------- сбор */

static void collect_target(struct flb_in_clickhouse *ctx, struct ch_target *t,
                           struct dbm_set *set)
{
    struct flb_connection *conn;
    struct flb_http_client *client;
    flb_sds_t query;
    flb_sds_t uri;
    size_t b_sent;
    int ret;
    const char *body;
    size_t body_len;
    const char *line;
    const char *nl;
    size_t line_len;
    char newest[CURSOR_MAX];
    int rows = 0;

    conn = flb_upstream_conn_get(t->u);
    if (!conn) {
        flb_plg_error(ctx->ins, "%s:%i недоступен", t->host, t->port);
        return;
    }

    query = build_query(ctx, t);
    if (!query) {
        flb_upstream_conn_release(conn);
        return;
    }

    uri = flb_sds_create("/");
    if (ctx->database && flb_sds_len(ctx->database) > 0) {
        flb_sds_printf(&uri, "?database=%s", ctx->database);
    }

    client = flb_http_client(conn, FLB_HTTP_POST, uri,
                             query, flb_sds_len(query),
                             t->host, t->port, NULL, 0);
    if (!client) {
        flb_sds_destroy(uri);
        flb_sds_destroy(query);
        flb_upstream_conn_release(conn);
        return;
    }

    /* по умолчанию клиент держит ответ в 4 КБ и на превышении роняет весь
     * запрос, а не обрезает его. Системные таблицы кластера — мегабайты */
    flb_http_buffer_size(client, ctx->buffer_max_size);

    if (ctx->user && flb_sds_len(ctx->user) > 0) {
        flb_http_add_header(client, "X-ClickHouse-User", 17,
                            ctx->user, flb_sds_len(ctx->user));
    }
    if (ctx->password && flb_sds_len(ctx->password) > 0) {
        flb_http_add_header(client, "X-ClickHouse-Key", 16,
                            ctx->password, flb_sds_len(ctx->password));
    }

    ret = flb_http_do(client, &b_sent);
    if (ret != 0 || client->resp.status != 200) {
        flb_plg_error(ctx->ins, "%s:%i вернул %i: %.*s",
                      t->host, t->port, client->resp.status,
                      client->resp.payload_size > 200 ? 200 : (int) client->resp.payload_size,
                      client->resp.payload ? client->resp.payload : "");
        goto done;
    }

    body = client->resp.payload;
    body_len = client->resp.payload_size;
    if (!body || body_len == 0) {
        goto done;
    }

    newest[0] = '\0';
    line = body;
    while (line < body + body_len) {
        nl = memchr(line, '\n', (body + body_len) - line);
        line_len = nl ? (size_t) (nl - line) : (size_t) ((body + body_len) - line);
        if (line_len > 0) {
            if (process_line(ctx, t, line, line_len, set,
                             newest, sizeof(newest)) == 0) {
                rows++;
            }
        }
        if (!nl) {
            break;
        }
        line = nl + 1;
    }

    if (rows > 0) {
        if (ctx->mode != DBM_MODE_METRICS) {
            flb_input_log_append(ctx->ins, NULL, 0,
                                 ctx->log_encoder.output_buffer,
                                 ctx->log_encoder.output_length);
            flb_log_event_encoder_reset(&ctx->log_encoder);
        }
        if (newest[0] != '\0') {
            memcpy(t->cursor, newest, sizeof(newest));
            cursor_save(ctx, t);
        }
        flb_plg_debug(ctx->ins, "%s:%i — строк %i", t->host, t->port, rows);
    }

done:
    flb_http_client_destroy(client);
    flb_sds_destroy(uri);
    flb_sds_destroy(query);
    flb_upstream_conn_release(conn);
}

static int cb_collect(struct flb_input_instance *ins,
                      struct flb_config *config, void *in_context)
{
    struct flb_in_clickhouse *ctx = in_context;
    struct mk_list *head;
    struct ch_target *t;
    struct dbm_set set;
    struct dbm_set *set_ptr = NULL;
    const char *tag = NULL;
    size_t tag_len = 0;

    (void) ins;
    (void) config;

    /* набор один на цикл: у метрики фиксированный набор меток, а ноды
     * различаются значением instance — им нужен общий gauge, а не свой */
    if (ctx->mode != DBM_MODE_LOGS) {
        if (dbm_set_init(&set, ctx->instance_field, &ctx->labels) != 0) {
            flb_plg_error(ctx->ins, "набор метрик не создан");
            return -1;
        }
        set_ptr = &set;
    }

    mk_list_foreach(head, &ctx->targets) {
        t = mk_list_entry(head, struct ch_target, _head);
        collect_target(ctx, t, set_ptr);
    }

    if (set_ptr) {
        /* свой тег нужен, когда включён режим both: одному выходу нельзя
         * отдать и записи, и метрики — разбирать их он будет по-разному */
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
    struct flb_in_clickhouse *ctx;
    int ret;

    (void) data;

    ctx = flb_calloc(1, sizeof(struct flb_in_clickhouse));
    if (!ctx) {
        flb_errno();
        return -1;
    }
    ctx->ins = ins;
    ctx->coll_fd = -1;
    mk_list_init(&ctx->targets);

    ret = flb_input_config_map_set(ins, (void *) ctx);
    if (ret == -1) {
        flb_free(ctx);
        return -1;
    }
    flb_input_set_context(ins, ctx);

    ctx->mode = dbm_mode_parse(ctx->mode_conf);
    if (ctx->mode < 0) {
        flb_plg_error(ins, "mode: logs, metrics или both (задано %s)", ctx->mode_conf);
        return -1;
    }
    ctx->cursor_type = dbm_cursor_type_parse(ctx->cursor_type_conf);
    if (ctx->cursor_type < 0) {
        flb_plg_error(ins, "cursor_type: datetime64, string, number или raw (задано %s)",
                      ctx->cursor_type_conf);
        return -1;
    }
    if (dbm_names_init(&ctx->labels, ctx->label_fields) != 0 ||
        dbm_names_init(&ctx->values, ctx->value_fields) != 0) {
        flb_plg_error(ins, "не разобраны label_fields/value_fields");
        return -1;
    }

    if (!ctx->targets_conf || flb_sds_len(ctx->targets_conf) == 0) {
        flb_plg_error(ins, "нужен параметр targets: host:port[,host:port]");
        return -1;
    }

    /* запрос: из файла либо строкой в конфиге */
    if (ctx->query_file && flb_sds_len(ctx->query_file) > 0) {
        ctx->sql = read_file(ins, ctx->query_file);
    }
    else if (ctx->query && flb_sds_len(ctx->query) > 0) {
        ctx->sql = flb_sds_create_len(ctx->query, flb_sds_len(ctx->query));
    }
    if (!ctx->sql) {
        flb_plg_error(ins, "нужен query или query_file");
        return -1;
    }


    if (parse_targets(ctx, config) != 0) {
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

    flb_plg_info(ins, "опрос каждые %i с, нод %i, режим %s",
                 ctx->interval_sec, mk_list_size(&ctx->targets), ctx->mode_conf);
    if (ctx->mode != DBM_MODE_LOGS) {
        flb_plg_info(ins, "метрики: префикс %s, метки %s%s%s",
                     ctx->metric_prefix,
                     ctx->instance_field && flb_sds_len(ctx->instance_field) > 0 ?
                         ctx->instance_field : "—",
                     ctx->labels.count > 0 ? ", " : "",
                     ctx->labels.count > 0 ? ctx->label_fields : "");
    }
    return 0;
}

static void cb_pause(void *data, struct flb_config *config)
{
    struct flb_in_clickhouse *ctx = data;
    (void) config;
    if (ctx->coll_fd >= 0) {
        flb_input_collector_pause(ctx->coll_fd, ctx->ins);
    }
}

static void cb_resume(void *data, struct flb_config *config)
{
    struct flb_in_clickhouse *ctx = data;
    (void) config;
    if (ctx->coll_fd >= 0) {
        flb_input_collector_resume(ctx->coll_fd, ctx->ins);
    }
}

static int cb_exit(void *data, struct flb_config *config)
{
    struct flb_in_clickhouse *ctx = data;
    struct mk_list *head;
    struct mk_list *tmp;
    struct ch_target *t;

    (void) config;

    if (!ctx) {
        return 0;
    }

    mk_list_foreach_safe(head, tmp, &ctx->targets) {
        t = mk_list_entry(head, struct ch_target, _head);
        mk_list_del(&t->_head);
        if (t->u) {
            flb_upstream_destroy(t->u);
        }
        flb_sds_destroy(t->host);
        flb_free(t);
    }

    if (ctx->coll_fd >= 0) {
        flb_log_event_encoder_destroy(&ctx->log_encoder);
    }
    if (ctx->sql) {
        flb_sds_destroy(ctx->sql);
    }
    dbm_names_destroy(&ctx->labels);
    dbm_names_destroy(&ctx->values);
    flb_free(ctx);
    return 0;
}

static struct flb_config_map config_map[] = {
    {
     FLB_CONFIG_MAP_STR, "targets", NULL,
     0, FLB_TRUE, offsetof(struct flb_in_clickhouse, targets_conf),
     "Ноды ClickHouse: host:port через запятую"
    },
    {
     FLB_CONFIG_MAP_STR, "query", NULL,
     0, FLB_TRUE, offsetof(struct flb_in_clickhouse, query),
     "Текст SQL-запроса"
    },
    {
     FLB_CONFIG_MAP_STR, "query_file", NULL,
     0, FLB_TRUE, offsetof(struct flb_in_clickhouse, query_file),
     "Файл с SQL-запросом (важнее, чем query)"
    },
    {
     FLB_CONFIG_MAP_STR, "user", NULL,
     0, FLB_TRUE, offsetof(struct flb_in_clickhouse, user),
     "Пользователь ClickHouse"
    },
    {
     FLB_CONFIG_MAP_STR, "password", NULL,
     0, FLB_TRUE, offsetof(struct flb_in_clickhouse, password),
     "Пароль ClickHouse"
    },
    {
     FLB_CONFIG_MAP_STR, "database", NULL,
     0, FLB_TRUE, offsetof(struct flb_in_clickhouse, database),
     "База по умолчанию"
    },
    {
     FLB_CONFIG_MAP_STR, "instance_field", "instance",
     0, FLB_TRUE, offsetof(struct flb_in_clickhouse, instance_field),
     "Поле с именем ноды; если запрос его не отдал (hostName()), плагин добавит сам"
    },
    {
     FLB_CONFIG_MAP_STR, "time_field", NULL,
     0, FLB_TRUE, offsetof(struct flb_in_clickhouse, time_field),
     "Поле со временем события; без него берётся время опроса"
    },
    {
     FLB_CONFIG_MAP_STR, "cursor_field", NULL,
     0, FLB_TRUE, offsetof(struct flb_in_clickhouse, cursor_field),
     "Поле ответа, из которого берётся курсор для {CURSOR}"
    },
    {
     FLB_CONFIG_MAP_STR, "cursor_default", "now() - INTERVAL 30 SECOND",
     0, FLB_TRUE, offsetof(struct flb_in_clickhouse, cursor_default),
     "Чем подставить {CURSOR} на первом запросе"
    },
    {
     FLB_CONFIG_MAP_STR, "cursor_file", NULL,
     0, FLB_TRUE, offsetof(struct flb_in_clickhouse, cursor_file),
     "Путь-основа для хранения курсора между перезапусками (на ноду свой файл)"
    },
    {
     FLB_CONFIG_MAP_STR, "cursor_type", "datetime64",
     0, FLB_TRUE, offsetof(struct flb_in_clickhouse, cursor_type_conf),
     "Как подставлять курсор: datetime64, string, number или raw"
    },
    {
     FLB_CONFIG_MAP_STR, "mode", "logs",
     0, FLB_TRUE, offsetof(struct flb_in_clickhouse, mode_conf),
     "Что отдавать: logs (записи), metrics (метрики) или both"
    },
    {
     FLB_CONFIG_MAP_STR, "metric_prefix", "clickhouse",
     0, FLB_TRUE, offsetof(struct flb_in_clickhouse, metric_prefix),
     "Общее начало имени метрик: <префикс>_<поле>"
    },
    {
     FLB_CONFIG_MAP_STR, "metrics_tag", NULL,
     0, FLB_TRUE, offsetof(struct flb_in_clickhouse, metrics_tag),
     "Тег метрик; нужен в режиме both, чтобы отделить их от записей"
    },
    {
     FLB_CONFIG_MAP_STR, "label_fields", NULL,
     0, FLB_TRUE, offsetof(struct flb_in_clickhouse, label_fields),
     "Поля-метки через запятую; instance добавляется сам"
    },
    {
     FLB_CONFIG_MAP_STR, "value_fields", NULL,
     0, FLB_TRUE, offsetof(struct flb_in_clickhouse, value_fields),
     "Поля-значения через запятую; пусто — все числовые, кроме меток"
    },
    {
     FLB_CONFIG_MAP_SIZE, "buffer_max_size", "0",
     0, FLB_TRUE, offsetof(struct flb_in_clickhouse, buffer_max_size),
     "Потолок ответа ClickHouse; 0 — без ограничения"
    },
    {
     FLB_CONFIG_MAP_INT, "interval_sec", DEFAULT_INTERVAL_SEC,
     0, FLB_TRUE, offsetof(struct flb_in_clickhouse, interval_sec),
     "Период опроса, секунды"
    },
    {
     FLB_CONFIG_MAP_INT, "interval_nsec", "0",
     0, FLB_TRUE, offsetof(struct flb_in_clickhouse, interval_nsec),
     "Период опроса, наносекунды"
    },
    {0}
};

struct flb_input_plugin in_clickhouse_plugin = {
    .name         = "clickhouse",
    .description  = "ClickHouse SQL (JSONEachRow) input: записи или метрики",
    .cb_init      = cb_init,
    .cb_pre_run   = NULL,
    .cb_collect   = cb_collect,
    .cb_flush_buf = NULL,
    .cb_pause     = cb_pause,
    .cb_resume    = cb_resume,
    .cb_exit      = cb_exit,
    .config_map   = config_map,
    /* FLB_INPUT_NET численно совпадает с FLB_IO_OPT_TLS, поэтому «tls по
     * желанию» ядро включает уже по нему. Дописывать FLB_IO_TLS нельзя: это
     * означает «всегда TLS», и плагин полез бы по HTTPS на обычный порт */
    .flags        = FLB_INPUT_NET,
};
