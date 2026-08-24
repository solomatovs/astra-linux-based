/* in_clickhouse — входной плагин: выполняет SQL в ClickHouse по HTTP, строки
 * ответа (JSONEachRow) становятся записями.
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

#define DEFAULT_INTERVAL_SEC  "5"
#define DEFAULT_PORT          8123
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
    flb_sds_t cursor_field;
    flb_sds_t cursor_default;
    flb_sds_t cursor_file;
    size_t buffer_max_size;
    int interval_sec;
    int interval_nsec;

    flb_sds_t sql;          /* текст запроса, прочитанный один раз */
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
            t->port = DEFAULT_PORT;
        }
        t->host = flb_sds_create(item);
        t->u = flb_upstream_create(config, t->host, t->port, FLB_IO_TCP, NULL);
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
        flb_sds_printf(&value, "toDateTime64('%s', 6)", t->cursor);
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

/* Значение строкового поля JSON без разбора всего объекта — нужно только курсору. */
static int json_str_field(const char *line, size_t len, const char *field,
                          char *out, size_t out_size)
{
    char key[128];
    const char *p;
    const char *end;
    size_t n;

    if (snprintf(key, sizeof(key), "\"%s\":", field) >= (int) sizeof(key)) {
        return -1;
    }
    p = strstr(line, key);
    if (!p || (size_t) (p - line) >= len) {
        return -1;
    }
    p += strlen(key);
    while (*p == ' ') {
        p++;
    }
    if (*p != '"') {
        return -1;
    }
    p++;
    end = strchr(p, '"');
    if (!end) {
        return -1;
    }
    n = (size_t) (end - p);
    if (n >= out_size) {
        n = out_size - 1;
    }
    memcpy(out, p, n);
    out[n] = '\0';
    return 0;
}

/* ------------------------------------------------------------------- сбор */

static int emit_line(struct flb_in_clickhouse *ctx, const char *line, size_t len)
{
    int ret;
    int root_type;
    char *mp_buf = NULL;
    size_t mp_size = 0;
    size_t consumed = 0;
    struct flb_time tm;

    ret = flb_pack_json(line, len, &mp_buf, &mp_size, &root_type, &consumed);
    if (ret != 0) {
        flb_plg_warn(ctx->ins, "строка ответа не разобрана как JSON");
        return -1;
    }

    flb_time_get(&tm);
    ret = flb_log_event_encoder_begin_record(&ctx->log_encoder);
    if (ret == FLB_EVENT_ENCODER_SUCCESS) {
        ret = flb_log_event_encoder_set_timestamp(&ctx->log_encoder, &tm);
    }
    if (ret == FLB_EVENT_ENCODER_SUCCESS) {
        ret = flb_log_event_encoder_set_body_from_raw_msgpack(&ctx->log_encoder,
                                                             mp_buf, mp_size);
    }
    if (ret == FLB_EVENT_ENCODER_SUCCESS) {
        ret = flb_log_event_encoder_commit_record(&ctx->log_encoder);
    }
    flb_free(mp_buf);

    if (ret != FLB_EVENT_ENCODER_SUCCESS) {
        flb_plg_error(ctx->ins, "запись не закодирована: %i", ret);
        return -1;
    }
    return 0;
}

static void collect_target(struct flb_in_clickhouse *ctx, struct ch_target *t)
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
            if (emit_line(ctx, line, line_len) == 0) {
                rows++;
                if (ctx->cursor_field && flb_sds_len(ctx->cursor_field) > 0) {
                    char value[CURSOR_MAX];
                    if (json_str_field(line, line_len, ctx->cursor_field,
                                       value, sizeof(value)) == 0) {
                        if (newest[0] == '\0' || strcmp(value, newest) > 0) {
                            memcpy(newest, value, sizeof(value));
                        }
                    }
                }
            }
        }
        if (!nl) {
            break;
        }
        line = nl + 1;
    }

    if (rows > 0) {
        flb_input_log_append(ctx->ins, NULL, 0,
                             ctx->log_encoder.output_buffer,
                             ctx->log_encoder.output_length);
        flb_log_event_encoder_reset(&ctx->log_encoder);
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

    (void) ins;
    (void) config;

    mk_list_foreach(head, &ctx->targets) {
        t = mk_list_entry(head, struct ch_target, _head);
        collect_target(ctx, t);
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
    mk_list_init(&ctx->targets);

    ret = flb_input_config_map_set(ins, (void *) ctx);
    if (ret == -1) {
        flb_free(ctx);
        return -1;
    }

    if (!ctx->targets_conf || flb_sds_len(ctx->targets_conf) == 0) {
        flb_plg_error(ins, "нужен параметр targets: host:port[,host:port]");
        flb_free(ctx);
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
        flb_free(ctx);
        return -1;
    }

    if (parse_targets(ctx, config) != 0) {
        flb_sds_destroy(ctx->sql);
        flb_free(ctx);
        return -1;
    }

    ret = flb_log_event_encoder_init(&ctx->log_encoder,
                                     FLB_LOG_EVENT_FORMAT_DEFAULT);
    if (ret != FLB_EVENT_ENCODER_SUCCESS) {
        flb_plg_error(ins, "кодировщик записей не поднялся: %i", ret);
        flb_sds_destroy(ctx->sql);
        flb_free(ctx);
        return -1;
    }

    flb_input_set_context(ins, ctx);

    ret = flb_input_set_collector_time(ins, cb_collect,
                                       ctx->interval_sec, ctx->interval_nsec,
                                       config);
    if (ret == -1) {
        flb_plg_error(ins, "таймер сбора не заведён");
        return -1;
    }
    ctx->coll_fd = ret;

    flb_plg_info(ins, "опрос каждые %i с, нод %i",
                 ctx->interval_sec, mk_list_size(&ctx->targets));
    return 0;
}

static void cb_pause(void *data, struct flb_config *config)
{
    struct flb_in_clickhouse *ctx = data;
    flb_input_collector_pause(ctx->coll_fd, ctx->ins);
    (void) config;
}

static void cb_resume(void *data, struct flb_config *config)
{
    struct flb_in_clickhouse *ctx = data;
    flb_input_collector_resume(ctx->coll_fd, ctx->ins);
    (void) config;
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

    flb_log_event_encoder_destroy(&ctx->log_encoder);
    if (ctx->sql) {
        flb_sds_destroy(ctx->sql);
    }
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
    .description  = "ClickHouse SQL (JSONEachRow) input",
    .cb_init      = cb_init,
    .cb_pre_run   = NULL,
    .cb_collect   = cb_collect,
    .cb_flush_buf = NULL,
    .cb_pause     = cb_pause,
    .cb_resume    = cb_resume,
    .cb_exit      = cb_exit,
    .config_map   = config_map,
    .flags        = FLB_INPUT_NET,
};
