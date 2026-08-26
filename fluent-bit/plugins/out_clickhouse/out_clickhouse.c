/* out_clickhouse — выходной плагин: пачка событий уезжает в ClickHouse по HTTP
 * как `FORMAT JSONEachRow`, а что с ней делать, описывает произвольный SQL.
 *
 * Два способа задать назначение:
 *   Table  — простая вставка, поля записи ложатся в одноимённые столбцы;
 *   Query  — любой SQL с `input('схема')`: агрегация, JOIN, несколько таблиц
 *            через CTE. Батч подставляется в input(), тело запроса не трогается.
 *
 * Значения не экранируются вручную и в текст запроса не попадают: они едут
 * телом HTTP как JSON, разбирает их сам сервер. Инъекции взяться неоткуда.
 *
 * Метрики (event_type metrics) разворачиваются в строки фиксированной формы
 * name/type/labels/value/timestamp — см. dbsink.h.
 *
 * Загрузчик ищет в .so структуру `out_clickhouse_plugin`.
 */

#include <fluent-bit/flb_output_plugin.h>
#include <fluent-bit/flb_http_client.h>
#include <fluent-bit/flb_upstream.h>
#include <fluent-bit/flb_config_map.h>
#include <fluent-bit/flb_sds.h>
#include <fluent-bit/tls/flb_tls.h>

#include "dbsink.h"

#define DEFAULT_PORT      8123
#define DEFAULT_PORT_TLS  8443

struct flb_out_clickhouse {
    struct flb_output_instance *ins;
    struct flb_upstream *u;

    /* конфигурация */
    flb_sds_t host;
    int port;
    flb_sds_t user;
    flb_sds_t password;
    flb_sds_t database;
    flb_sds_t table;
    flb_sds_t query;
    flb_sds_t query_file;
    flb_sds_t settings;
    flb_sds_t time_key;
    flb_sds_t time_format_conf;
    int skip_unknown;

    int time_format;
    flb_sds_t uri;          /* URI со вставкой, собран один раз */
};

/* Значение в URL: тело занято данными, поэтому запрос едет параметром */
static flb_sds_t url_encode(const char *in, size_t len)
{
    static const char hex[] = "0123456789ABCDEF";
    flb_sds_t out;
    size_t i;
    char c;
    char enc[3];

    out = flb_sds_create_size(len * 3 + 1);
    if (!out) {
        return NULL;
    }
    for (i = 0; i < len; i++) {
        c = in[i];
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
            (c >= '0' && c <= '9') || c == '-' || c == '_' ||
            c == '.' || c == '~') {
            flb_sds_cat_safe(&out, &c, 1);
        }
        else {
            enc[0] = '%';
            enc[1] = hex[(unsigned char) c >> 4];
            enc[2] = hex[(unsigned char) c & 0x0f];
            flb_sds_cat_safe(&out, enc, 3);
        }
    }
    return out;
}

static void append_settings(flb_sds_t *uri, const char *settings)
{
    const char *p = settings;
    const char *item;
    size_t len;

    while (*p) {
        while (*p == ',' || *p == ' ') {
            p++;
        }
        item = p;
        while (*p && *p != ',') {
            p++;
        }
        len = (size_t) (p - item);
        while (len > 0 && item[len - 1] == ' ') {
            len--;
        }
        if (len > 0) {
            flb_sds_cat_safe(uri, "&", 1);
            flb_sds_cat_safe(uri, item, len);
        }
    }
}

static flb_sds_t read_file(struct flb_output_instance *ins, const char *path)
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

static int cb_init(struct flb_output_instance *ins, struct flb_config *config,
                   void *data)
{
    struct flb_out_clickhouse *ctx;
    flb_sds_t sql;
    flb_sds_t encoded;
    const char *tmp;
    int io_flags;
    int ret;

    (void) data;

    ctx = flb_calloc(1, sizeof(struct flb_out_clickhouse));
    if (!ctx) {
        flb_errno();
        return -1;
    }
    ctx->ins = ins;
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

    /* адрес: у сетевых выходов (FLB_OUTPUT_NET) ядро разбирает Host/Port само
     * и кладёт в ins->host — в списке свойств их уже нет */
    tmp = ins->host.name;
    ctx->host = flb_sds_create(tmp ? tmp : "127.0.0.1");
    ctx->port = ins->host.port;
    if (ctx->port == 0) {
        ctx->port = ins->use_tls ? DEFAULT_PORT_TLS : DEFAULT_PORT;
    }

    /* запрос: из файла, строкой или собранный из table */
    if (ctx->query_file && flb_sds_len(ctx->query_file) > 0) {
        sql = read_file(ins, ctx->query_file);
        if (!sql) {
            return -1;
        }
    }
    else if (ctx->query && flb_sds_len(ctx->query) > 0) {
        sql = flb_sds_create_len(ctx->query, flb_sds_len(ctx->query));
    }
    else if (ctx->table && flb_sds_len(ctx->table) > 0) {
        sql = flb_sds_create_size(64);
        if (sql) {
            flb_sds_printf(&sql, "INSERT INTO %s", ctx->table);
        }
    }
    else {
        flb_plg_error(ins, "нужен table, query или query_file");
        return -1;
    }
    if (!sql) {
        return -1;
    }

    /* FORMAT JSONEachRow дописывается сам: формат задаёт плагин, а не автор
     * запроса — тело он всё равно не выбирает */
    if (!strstr(sql, "FORMAT ") && !strstr(sql, "format ")) {
        flb_sds_printf(&sql, " FORMAT JSONEachRow");
    }

    encoded = url_encode(sql, flb_sds_len(sql));
    flb_sds_destroy(sql);
    if (!encoded) {
        return -1;
    }

    ctx->uri = flb_sds_create_size(flb_sds_len(encoded) + 160);
    if (!ctx->uri) {
        flb_sds_destroy(encoded);
        return -1;
    }
    flb_sds_printf(&ctx->uri, "/?query=%s", encoded);
    flb_sds_destroy(encoded);

    if (ctx->database && flb_sds_len(ctx->database) > 0) {
        flb_sds_printf(&ctx->uri, "&database=%s", ctx->database);
    }
    /* поля записи и столбцы таблицы совпадают редко: лишнее в JSONEachRow по
     * умолчанию — ошибка всей вставки, а не пропуск поля */
    if (ctx->skip_unknown) {
        flb_sds_printf(&ctx->uri, "&input_format_skip_unknown_fields=1");
    }
    if (ctx->settings && flb_sds_len(ctx->settings) > 0) {
        append_settings(&ctx->uri, ctx->settings);
    }

    /* TLS настраивает ядро: tls/tls.verify/tls.ca_file — его свойства, до
     * config_map плагина они не доходят. Плагину остаётся взять готовый
     * ins->tls и поднять флаг апстрима */
    io_flags = FLB_IO_TCP;
    if (ins->use_tls) {
        io_flags |= FLB_IO_TLS;
    }

    ctx->u = flb_upstream_create(config, ctx->host, ctx->port, io_flags, ins->tls);
    if (!ctx->u) {
        flb_plg_error(ins, "не создано соединение с %s:%i", ctx->host, ctx->port);
        return -1;
    }
    flb_output_upstream_set(ctx->u, ins);

    flb_plg_info(ins, "%s:%i, время в поле %s (%s)", ctx->host, ctx->port,
                 ctx->time_key && flb_sds_len(ctx->time_key) > 0 ?
                     ctx->time_key : "—",
                 ctx->time_format_conf);
    return 0;
}

static void cb_flush(struct flb_event_chunk *event_chunk,
                     struct flb_output_flush *out_flush,
                     struct flb_input_instance *i_ins,
                     void *out_context, struct flb_config *config)
{
    struct flb_out_clickhouse *ctx = out_context;
    struct flb_connection *conn;
    struct flb_http_client *client;
    flb_sds_t body;
    size_t b_sent;
    int rows;
    int ret;
    int status;

    (void) i_ins;
    (void) config;

    body = flb_sds_create_size(4096);
    if (!body) {
        FLB_OUTPUT_RETURN(FLB_RETRY);
    }

    rows = dbsink_chunk_json(event_chunk, ctx->time_key, ctx->time_format,
                             "\n", &body);
    if (rows < 0) {
        flb_plg_error(ctx->ins, "пачка событий не разобрана");
        flb_sds_destroy(body);
        FLB_OUTPUT_RETURN(FLB_ERROR);
    }
    if (rows == 0) {
        flb_sds_destroy(body);
        FLB_OUTPUT_RETURN(FLB_OK);
    }

    conn = flb_upstream_conn_get(ctx->u);
    if (!conn) {
        flb_plg_error(ctx->ins, "%s:%i недоступен", ctx->host, ctx->port);
        flb_sds_destroy(body);
        FLB_OUTPUT_RETURN(FLB_RETRY);
    }

    client = flb_http_client(conn, FLB_HTTP_POST, ctx->uri,
                             body, flb_sds_len(body),
                             ctx->host, ctx->port, NULL, 0);
    if (!client) {
        flb_upstream_conn_release(conn);
        flb_sds_destroy(body);
        FLB_OUTPUT_RETURN(FLB_RETRY);
    }
    flb_http_buffer_size(client, 0);

    if (ctx->user && flb_sds_len(ctx->user) > 0) {
        flb_http_add_header(client, "X-ClickHouse-User", 17,
                            ctx->user, flb_sds_len(ctx->user));
    }
    if (ctx->password && flb_sds_len(ctx->password) > 0) {
        flb_http_add_header(client, "X-ClickHouse-Key", 16,
                            ctx->password, flb_sds_len(ctx->password));
    }

    ret = flb_http_do(client, &b_sent);
    status = client->resp.status;

    if (ret != 0) {
        flb_plg_error(ctx->ins, "%s:%i — запрос не ушёл", ctx->host, ctx->port);
        flb_http_client_destroy(client);
        flb_upstream_conn_release(conn);
        flb_sds_destroy(body);
        FLB_OUTPUT_RETURN(FLB_RETRY);
    }

    if (status != 200) {
        flb_plg_error(ctx->ins, "%s:%i вернул %i: %.*s", ctx->host, ctx->port,
                      status,
                      client->resp.payload_size > 300 ? 300 : (int) client->resp.payload_size,
                      client->resp.payload ? client->resp.payload : "");
    }
    else {
        flb_plg_debug(ctx->ins, "вставлено строк %i, байт %zu",
                      rows, flb_sds_len(body));
    }

    flb_http_client_destroy(client);
    flb_upstream_conn_release(conn);
    flb_sds_destroy(body);

    if (status == 200) {
        FLB_OUTPUT_RETURN(FLB_OK);
    }
    /* 4xx — это про сам запрос: схема, права, синтаксис. Повтор не поможет,
     * а место в буфере займёт. 5xx и обрывы — повторяем */
    if (status >= 400 && status < 500) {
        FLB_OUTPUT_RETURN(FLB_ERROR);
    }
    FLB_OUTPUT_RETURN(FLB_RETRY);
}

static int cb_exit(void *data, struct flb_config *config)
{
    struct flb_out_clickhouse *ctx = data;

    (void) config;

    if (!ctx) {
        return 0;
    }
    if (ctx->u) {
        flb_upstream_destroy(ctx->u);
    }
    flb_sds_destroy(ctx->host);
    flb_sds_destroy(ctx->uri);
    flb_free(ctx);
    return 0;
}

static struct flb_config_map config_map[] = {
    {
     FLB_CONFIG_MAP_STR, "user", NULL,
     0, FLB_TRUE, offsetof(struct flb_out_clickhouse, user),
     "Пользователь ClickHouse"
    },
    {
     FLB_CONFIG_MAP_STR, "password", NULL,
     0, FLB_TRUE, offsetof(struct flb_out_clickhouse, password),
     "Пароль ClickHouse"
    },
    {
     FLB_CONFIG_MAP_STR, "database", NULL,
     0, FLB_TRUE, offsetof(struct flb_out_clickhouse, database),
     "База по умолчанию"
    },
    {
     FLB_CONFIG_MAP_STR, "table", NULL,
     0, FLB_TRUE, offsetof(struct flb_out_clickhouse, table),
     "Таблица для простой вставки: поля записи → одноимённые столбцы"
    },
    {
     FLB_CONFIG_MAP_STR, "query", NULL,
     0, FLB_TRUE, offsetof(struct flb_out_clickhouse, query),
     "Произвольный SQL; пачка подставляется в input('схема')"
    },
    {
     FLB_CONFIG_MAP_STR, "query_file", NULL,
     0, FLB_TRUE, offsetof(struct flb_out_clickhouse, query_file),
     "Файл с запросом (важнее, чем query и table)"
    },
    {
     FLB_CONFIG_MAP_STR, "settings", NULL,
     0, FLB_TRUE, offsetof(struct flb_out_clickhouse, settings),
     "Настройки ClickHouse для вставки, key=value через запятую"
    },
    {
     FLB_CONFIG_MAP_BOOL, "skip_unknown_fields", "on",
     0, FLB_TRUE, offsetof(struct flb_out_clickhouse, skip_unknown),
     "Игнорировать поля записи, которым нет места в схеме"
    },
    {
     FLB_CONFIG_MAP_STR, "time_key", "timestamp",
     0, FLB_TRUE, offsetof(struct flb_out_clickhouse, time_key),
     "Поле, куда положить время события; пустое — не добавлять"
    },
    {
     FLB_CONFIG_MAP_STR, "time_format", "datetime64",
     0, FLB_TRUE, offsetof(struct flb_out_clickhouse, time_format_conf),
     "Формат времени: datetime64, iso8601 или epoch"
    },
    {0}
};

struct flb_output_plugin out_clickhouse_plugin = {
    .name         = "clickhouse",
    .description  = "ClickHouse output: записи и метрики произвольным SQL",
    .cb_init      = cb_init,
    .cb_flush     = cb_flush,
    .cb_exit      = cb_exit,
    .config_map   = config_map,
    .event_type   = FLB_OUTPUT_LOGS | FLB_OUTPUT_METRICS,
    .flags        = FLB_OUTPUT_NET | FLB_IO_OPT_TLS,
};
