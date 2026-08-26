/* in_pg2ch — насос PostgreSQL → ClickHouse: PG `COPY (SELECT ...) TO STDOUT`
 * (text-формат) батчами перекладывается в CH `INSERT ... FORMAT TabSeparated`.
 * Форматы почти байт-совместимы: \t, \N, перевод строки и backslash-эскейпы
 * совпадают, поэтому данные идут без построчного разбора.
 *
 * В конвейер fluent-bit данные НЕ попадают — только запись-отчёт о прогоне.
 * Инкремент — high-water mark в целевой таблице: Cursor_Query спрашивает у CH
 * отметку, она подставляется в {CURSOR} запроса к PG. Упавший прогон безопасен:
 * доехавшие батчи подняли отметку, следующий тик продолжит с места падения —
 * поэтому запрос ОБЯЗАН быть упорядочен по столбцу отметки (ORDER BY).
 *
 * Загрузчик ищет в .so структуру `in_pg2ch_plugin`. libpq не линкуется: она в
 * бинарнике ради out_pgsql, ENABLE_EXPORTS делает PQ* видимыми (в т.ч. GSSAPI).
 */

#include <fluent-bit/flb_input_plugin.h>
#include <fluent-bit/flb_http_client.h>
#include <fluent-bit/flb_upstream.h>
#include <fluent-bit/flb_time.h>
#include <fluent-bit/flb_config_map.h>
#include <fluent-bit/flb_sds.h>
#include <fluent-bit/flb_log_event_encoder.h>

#include <libpq-fe.h>

/* ради расписания: dbm_schedule_* поверх ccronexpr */
#include "dbmetrics.h"

#define DEFAULT_INTERVAL_SEC  "60"
#define DEFAULT_PG_PORT       "5432"
#define DEFAULT_CH_PORT       8123
#define DEFAULT_CH_PORT_TLS   8443
#define CURSOR_MAX            128

struct flb_in_pg2ch {
    struct flb_input_instance *ins;

    /* источник */
    flb_sds_t pg_target;
    flb_sds_t pg_user;
    flb_sds_t pg_password;
    flb_sds_t pg_database;
    flb_sds_t pg_options;
    flb_sds_t query;
    flb_sds_t query_file;

    /* приёмник */
    flb_sds_t ch_target;
    flb_sds_t ch_user;
    flb_sds_t ch_password;
    flb_sds_t ch_database;
    flb_sds_t ch_table;
    flb_sds_t ch_columns;
    flb_sds_t ch_settings;

    /* инкремент и режим */
    flb_sds_t cursor_query;
    flb_sds_t cursor_default;
    size_t batch_bytes;
    int report_empty;
    int connect_timeout;
    int statement_timeout;
    int retry_pause_sec;
    int interval_sec;
    int interval_nsec;

    /* состояние */
    flb_sds_t sql;              /* текст запроса, прочитанный один раз */
    flb_sds_t pg_host;
    flb_sds_t pg_port;
    PGconn *conn;
    time_t retry_after;
    flb_sds_t ch_host;
    int ch_port;
    struct flb_upstream *u;
    flb_sds_t insert_uri;       /* URI вставки, собранный один раз */
    flb_sds_t schedule_conf;
    struct dbm_schedule schedule;
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

/* Значение в URL: percent-encoding всего, кроме unreserved. Запрос вставки
 * едет в параметре query, тело HTTP занято самими данными. */
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

/* "key=value,key=value" из конфигурации → "&key=value&key=value" для URL */
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

/* -------------------------------------------------------------- подключения */

static void conninfo_add(flb_sds_t *out, const char *key, const char *value)
{
    const char *p;

    flb_sds_printf(out, "%s='", key);
    for (p = value; *p != '\0'; p++) {
        if (*p == '\'' || *p == '\\') {
            flb_sds_cat_safe(out, "\\", 1);
        }
        flb_sds_cat_safe(out, p, 1);
    }
    flb_sds_cat_safe(out, "' ", 2);
}

static int pg_connect(struct flb_in_pg2ch *ctx)
{
    flb_sds_t conninfo;
    char timeout[32];
    char options[64];

    if (!ctx->conn && ctx->retry_after > time(NULL)) {
        return -1;
    }

    if (ctx->conn) {
        if (PQstatus(ctx->conn) == CONNECTION_OK) {
            return 0;
        }
        PQreset(ctx->conn);
        if (PQstatus(ctx->conn) == CONNECTION_OK) {
            flb_plg_info(ctx->ins, "pg %s:%s — соединение восстановлено",
                         ctx->pg_host, ctx->pg_port);
            return 0;
        }
        PQfinish(ctx->conn);
        ctx->conn = NULL;
    }

    snprintf(timeout, sizeof(timeout), "%i", ctx->connect_timeout);
    snprintf(options, sizeof(options), "-c statement_timeout=%i",
             ctx->statement_timeout);

    conninfo = flb_sds_create_size(256);
    if (!conninfo) {
        return -1;
    }
    conninfo_add(&conninfo, "host", ctx->pg_host);
    conninfo_add(&conninfo, "port", ctx->pg_port);
    conninfo_add(&conninfo, "connect_timeout", timeout);
    conninfo_add(&conninfo, "options", options);
    conninfo_add(&conninfo, "application_name", "fluent-bit in_pg2ch");
    if (ctx->pg_database && flb_sds_len(ctx->pg_database) > 0) {
        conninfo_add(&conninfo, "dbname", ctx->pg_database);
    }
    if (ctx->pg_user && flb_sds_len(ctx->pg_user) > 0) {
        conninfo_add(&conninfo, "user", ctx->pg_user);
    }
    if (ctx->pg_password && flb_sds_len(ctx->pg_password) > 0) {
        conninfo_add(&conninfo, "password", ctx->pg_password);
    }
    if (ctx->pg_options && flb_sds_len(ctx->pg_options) > 0) {
        flb_sds_printf(&conninfo, "%s", ctx->pg_options);
    }

    ctx->conn = PQconnectdb(conninfo);
    flb_sds_destroy(conninfo);

    if (!ctx->conn || PQstatus(ctx->conn) != CONNECTION_OK) {
        flb_plg_error(ctx->ins, "pg %s:%s — не подключиться (пауза %i с): %s",
                      ctx->pg_host, ctx->pg_port, ctx->retry_pause_sec,
                      ctx->conn ? PQerrorMessage(ctx->conn) : "нет памяти");
        if (ctx->conn) {
            PQfinish(ctx->conn);
            ctx->conn = NULL;
        }
        ctx->retry_after = time(NULL) + ctx->retry_pause_sec;
        return -1;
    }

    ctx->retry_after = 0;
    flb_plg_info(ctx->ins, "pg %s:%s — подключён (%s)", ctx->pg_host,
                 ctx->pg_port, PQparameterStatus(ctx->conn, "server_version"));
    return 0;
}

/* Один HTTP-запрос к ClickHouse: запрос в URL, данные (или пусто) в теле.
 * Ответ (нужен курсору и текстам ошибок) копируется в resp. */
static int ch_request(struct flb_in_pg2ch *ctx, const char *uri,
                      const char *body, size_t body_len, flb_sds_t *resp)
{
    struct flb_connection *conn;
    struct flb_http_client *client;
    size_t b_sent;
    int ret;
    int status = -1;

    conn = flb_upstream_conn_get(ctx->u);
    if (!conn) {
        flb_plg_error(ctx->ins, "ch %s:%i недоступен", ctx->ch_host, ctx->ch_port);
        return -1;
    }

    client = flb_http_client(conn, FLB_HTTP_POST, uri, body, body_len,
                             ctx->ch_host, ctx->ch_port, NULL, 0);
    if (!client) {
        flb_upstream_conn_release(conn);
        return -1;
    }
    flb_http_buffer_size(client, 0);

    if (ctx->ch_user && flb_sds_len(ctx->ch_user) > 0) {
        flb_http_add_header(client, "X-ClickHouse-User", 17,
                            ctx->ch_user, flb_sds_len(ctx->ch_user));
    }
    if (ctx->ch_password && flb_sds_len(ctx->ch_password) > 0) {
        flb_http_add_header(client, "X-ClickHouse-Key", 16,
                            ctx->ch_password, flb_sds_len(ctx->ch_password));
    }

    ret = flb_http_do(client, &b_sent);
    if (ret == 0) {
        status = client->resp.status;
        if (resp && client->resp.payload && client->resp.payload_size > 0) {
            *resp = flb_sds_create_len(client->resp.payload,
                                       client->resp.payload_size);
        }
    }

    flb_http_client_destroy(client);
    flb_upstream_conn_release(conn);
    return status;
}

/* ------------------------------------------------------------------- прогон */

/* Отметка из CH: одна строка ответа Cursor_Query. Пустой ответ или ошибка —
 * значение по умолчанию: так первый прогон стартует на пустой целевой таблице. */
static void load_cursor(struct flb_in_pg2ch *ctx, char *out, size_t out_size)
{
    flb_sds_t q;
    flb_sds_t uri;
    flb_sds_t resp = NULL;
    int status;
    size_t n;

    snprintf(out, out_size, "%s", ctx->cursor_default);

    if (!ctx->cursor_query || flb_sds_len(ctx->cursor_query) == 0) {
        return;
    }

    q = url_encode(ctx->cursor_query, flb_sds_len(ctx->cursor_query));
    if (!q) {
        return;
    }
    uri = flb_sds_create_size(flb_sds_len(q) + 64);
    if (!uri) {
        flb_sds_destroy(q);
        return;
    }
    flb_sds_printf(&uri, "/?query=%s&default_format=TabSeparated", q);
    flb_sds_destroy(q);

    status = ch_request(ctx, uri, "", 0, &resp);
    flb_sds_destroy(uri);

    if (status != 200) {
        flb_plg_warn(ctx->ins, "cursor_query вернул %i%s%.*s — беру %s",
                     status, resp ? ": " : "",
                     resp ? (int) (flb_sds_len(resp) > 200 ? 200 : flb_sds_len(resp)) : 0,
                     resp ? resp : "", ctx->cursor_default);
        if (resp) {
            flb_sds_destroy(resp);
        }
        return;
    }

    if (resp) {
        n = flb_sds_len(resp);
        while (n > 0 && (resp[n - 1] == '\n' || resp[n - 1] == '\t' ||
                         resp[n - 1] == ' ')) {
            n--;
        }
        if (n > 0 && n < out_size) {
            memcpy(out, resp, n);
            out[n] = '\0';
        }
        flb_sds_destroy(resp);
    }
}

/* COPY (<sql с {CURSOR}>) TO STDOUT. Кавычки вокруг курсора не добавляются:
 * отметка приезжает из CH готовым литералом, а числовой в кавычках сравнился
 * бы строкой. Нужны кавычки — они пишутся в самом запросе: '{CURSOR}'. */
static flb_sds_t build_copy_sql(struct flb_in_pg2ch *ctx, const char *cursor)
{
    flb_sds_t out;
    const char *p;
    const char *mark;

    out = flb_sds_create_size(flb_sds_len(ctx->sql) + 64);
    if (!out) {
        return NULL;
    }
    flb_sds_cat_safe(&out, "COPY (", 6);

    p = ctx->sql;
    while ((mark = strstr(p, "{CURSOR}")) != NULL) {
        flb_sds_cat_safe(&out, p, mark - p);
        flb_sds_cat_safe(&out, cursor, strlen(cursor));
        p = mark + strlen("{CURSOR}");
    }
    flb_sds_cat_safe(&out, p, strlen(p));
    flb_sds_cat_safe(&out, ") TO STDOUT", 11);
    return out;
}

static int flush_batch(struct flb_in_pg2ch *ctx, flb_sds_t batch)
{
    flb_sds_t resp = NULL;
    int status;

    status = ch_request(ctx, ctx->insert_uri, batch, flb_sds_len(batch), &resp);
    if (status != 200) {
        flb_plg_error(ctx->ins, "вставка в %s вернула %i%s%.*s",
                      ctx->ch_table, status, resp ? ": " : "",
                      resp ? (int) (flb_sds_len(resp) > 300 ? 300 : flb_sds_len(resp)) : 0,
                      resp ? resp : "");
        if (resp) {
            flb_sds_destroy(resp);
        }
        return -1;
    }
    if (resp) {
        flb_sds_destroy(resp);
    }
    return 0;
}

/* Запись-отчёт: одна на прогон, чтобы за насосом можно было следить обычным
 * конвейером (stdout, es, log_to_metrics). */
static void report(struct flb_in_pg2ch *ctx, const char *cursor,
                   long rows, long bytes, int batches, double duration_ms,
                   const char *error)
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
            FLB_LOG_EVENT_CSTRING_VALUE("table"),
            FLB_LOG_EVENT_CSTRING_VALUE(ctx->ch_table),
            FLB_LOG_EVENT_CSTRING_VALUE("status"),
            FLB_LOG_EVENT_CSTRING_VALUE(error ? "error" : "ok"),
            FLB_LOG_EVENT_CSTRING_VALUE("rows"),
            FLB_LOG_EVENT_INT64_VALUE(rows),
            FLB_LOG_EVENT_CSTRING_VALUE("bytes"),
            FLB_LOG_EVENT_INT64_VALUE(bytes),
            FLB_LOG_EVENT_CSTRING_VALUE("batches"),
            FLB_LOG_EVENT_INT64_VALUE(batches),
            FLB_LOG_EVENT_CSTRING_VALUE("duration_ms"),
            FLB_LOG_EVENT_DOUBLE_VALUE(duration_ms),
            FLB_LOG_EVENT_CSTRING_VALUE("cursor"),
            FLB_LOG_EVENT_CSTRING_VALUE(cursor),
            FLB_LOG_EVENT_CSTRING_VALUE("error"),
            FLB_LOG_EVENT_CSTRING_VALUE(error ? error : ""));
    }
    if (ret == FLB_EVENT_ENCODER_SUCCESS) {
        ret = flb_log_event_encoder_commit_record(&ctx->log_encoder);
    }
    if (ret != FLB_EVENT_ENCODER_SUCCESS) {
        flb_log_event_encoder_rollback_record(&ctx->log_encoder);
        return;
    }
    flb_input_log_append(ctx->ins, NULL, 0,
                         ctx->log_encoder.output_buffer,
                         ctx->log_encoder.output_length);
    flb_log_event_encoder_reset(&ctx->log_encoder);
}

static int cb_collect(struct flb_input_instance *ins,
                      struct flb_config *config, void *in_context)
{
    struct flb_in_pg2ch *ctx = in_context;
    char cursor[CURSOR_MAX];
    flb_sds_t sql;
    flb_sds_t batch;
    PGresult *res;
    char *row = NULL;
    int n;
    long rows = 0;
    long bytes = 0;
    int batches = 0;
    int failed = 0;
    const char *error = NULL;
    struct flb_time t0, t1;
    double duration_ms;

    (void) ins;
    (void) config;

    /* расписание: тик пришёл, но момент ещё не наступил */
    if (!dbm_schedule_due(&ctx->schedule, time(NULL))) {
        return 0;
    }

    if (pg_connect(ctx) != 0) {
        return 0;
    }

    flb_time_get(&t0);
    load_cursor(ctx, cursor, sizeof(cursor));

    sql = build_copy_sql(ctx, cursor);
    if (!sql) {
        return -1;
    }

    res = PQexec(ctx->conn, sql);
    flb_sds_destroy(sql);
    if (!res || PQresultStatus(res) != PGRES_COPY_OUT) {
        flb_plg_error(ctx->ins, "COPY не начался: %s",
                      res ? PQresultErrorMessage(res) : PQerrorMessage(ctx->conn));
        if (res) {
            PQclear(res);
        }
        if (PQstatus(ctx->conn) != CONNECTION_OK) {
            PQfinish(ctx->conn);
            ctx->conn = NULL;
        }
        report(ctx, cursor, 0, 0, 0, 0, "copy failed to start");
        return 0;
    }
    PQclear(res);

    batch = flb_sds_create_size(ctx->batch_bytes > 0 ? ctx->batch_bytes : 65536);
    if (!batch) {
        return -1;
    }

    /* PQgetCopyData отдаёт по строке таблицы за вызов (с завершающим \n),
     * поэтому граница батча всегда совпадает с границей строки */
    while ((n = PQgetCopyData(ctx->conn, &row, 0)) > 0) {
        if (!failed) {
            flb_sds_cat_safe(&batch, row, n);
            rows++;
            bytes += n;
            if (ctx->batch_bytes > 0 && flb_sds_len(batch) >= ctx->batch_bytes) {
                if (flush_batch(ctx, batch) != 0) {
                    /* дальнейшие строки дочитываются и выбрасываются: COPY
                     * нельзя бросить на середине, не убив соединение. Отметку
                     * подняли только доехавшие батчи — следующий прогон
                     * продолжит ровно с них */
                    failed = 1;
                    error = "insert failed";
                }
                else {
                    batches++;
                }
                flb_sds_len_set(batch, 0);
            }
        }
        PQfreemem(row);
        row = NULL;
    }

    if (n == -2) {
        flb_plg_error(ctx->ins, "COPY оборвался: %s", PQerrorMessage(ctx->conn));
        if (!error) {
            error = "copy aborted";
        }
        PQfinish(ctx->conn);
        ctx->conn = NULL;
    }
    else {
        while ((res = PQgetResult(ctx->conn)) != NULL) {
            if (PQresultStatus(res) != PGRES_COMMAND_OK && !error) {
                flb_plg_error(ctx->ins, "COPY завершился с ошибкой: %s",
                              PQresultErrorMessage(res));
                error = "copy failed";
            }
            PQclear(res);
        }
    }

    if (!failed && !error && flb_sds_len(batch) > 0) {
        if (flush_batch(ctx, batch) != 0) {
            error = "insert failed";
        }
        else {
            batches++;
        }
    }
    flb_sds_destroy(batch);

    flb_time_get(&t1);
    duration_ms = (flb_time_to_nanosec(&t1) - flb_time_to_nanosec(&t0)) / 1e6;

    if (rows > 0 || error || ctx->report_empty) {
        report(ctx, cursor, rows, bytes, batches, duration_ms, error);
    }
    if (rows > 0 || error) {
        flb_plg_info(ctx->ins, "%s: строк %ld, байт %ld, батчей %i, %.0f мс%s%s",
                     ctx->ch_table, rows, bytes, batches, duration_ms,
                     error ? ", ошибка: " : "", error ? error : "");
    }
    return 0;
}

/* -------------------------------------------------------------- жизненный цикл */

static int cb_init(struct flb_input_instance *ins,
                   struct flb_config *config, void *data)
{
    struct flb_in_pg2ch *ctx;
    flb_sds_t q;
    flb_sds_t insert;
    char *colon;
    int io_flags;
    int ret;

    (void) data;

    ctx = flb_calloc(1, sizeof(struct flb_in_pg2ch));
    if (!ctx) {
        flb_errno();
        return -1;
    }
    ctx->ins = ins;
    ctx->coll_fd = -1;

    ret = flb_input_config_map_set(ins, (void *) ctx);
    if (ret == -1) {
        flb_free(ctx);
        return -1;
    }
    flb_input_set_context(ins, ctx);

    if (!ctx->pg_target || flb_sds_len(ctx->pg_target) == 0 ||
        !ctx->ch_target || flb_sds_len(ctx->ch_target) == 0 ||
        !ctx->ch_table || flb_sds_len(ctx->ch_table) == 0) {
        flb_plg_error(ins, "нужны pg_target, ch_target и ch_table");
        return -1;
    }

    /* pg: host[:port] */
    colon = strrchr(ctx->pg_target, ':');
    if (colon) {
        ctx->pg_host = flb_sds_create_len(ctx->pg_target,
                                          colon - ctx->pg_target);
        ctx->pg_port = flb_sds_create(colon + 1);
    }
    else {
        ctx->pg_host = flb_sds_create(ctx->pg_target);
        ctx->pg_port = flb_sds_create(DEFAULT_PG_PORT);
    }

    /* ch: host[:port] */
    colon = strrchr(ctx->ch_target, ':');
    if (colon) {
        ctx->ch_host = flb_sds_create_len(ctx->ch_target,
                                          colon - ctx->ch_target);
        ctx->ch_port = atoi(colon + 1);
    }
    else {
        ctx->ch_host = flb_sds_create(ctx->ch_target);
    }
    if (ctx->ch_port <= 0) {
        ctx->ch_port = ins->use_tls ? DEFAULT_CH_PORT_TLS : DEFAULT_CH_PORT;
    }

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

    /* URI вставки собирается один раз: он не зависит от прогона */
    insert = flb_sds_create_size(256);
    if (!insert) {
        return -1;
    }
    flb_sds_printf(&insert, "INSERT INTO %s ", ctx->ch_table);
    if (ctx->ch_columns && flb_sds_len(ctx->ch_columns) > 0) {
        flb_sds_printf(&insert, "(%s) ", ctx->ch_columns);
    }
    flb_sds_cat_safe(&insert, "FORMAT TabSeparated", 19);

    q = url_encode(insert, flb_sds_len(insert));
    flb_sds_destroy(insert);
    if (!q) {
        return -1;
    }
    ctx->insert_uri = flb_sds_create_size(flb_sds_len(q) + 128);
    if (!ctx->insert_uri) {
        flb_sds_destroy(q);
        return -1;
    }
    flb_sds_printf(&ctx->insert_uri, "/?query=%s", q);
    flb_sds_destroy(q);
    if (ctx->ch_database && flb_sds_len(ctx->ch_database) > 0) {
        flb_sds_printf(&ctx->insert_uri, "&database=%s", ctx->ch_database);
    }
    if (ctx->ch_settings && flb_sds_len(ctx->ch_settings) > 0) {
        append_settings(&ctx->insert_uri, ctx->ch_settings);
    }

    /* TLS до ClickHouse настраивает ядро (tls/tls.verify/...), плагину
     * остаётся поднять флаг и взять готовый контекст */
    io_flags = FLB_IO_TCP;
    if (ins->use_tls) {
        io_flags |= FLB_IO_TLS;
    }
    ctx->u = flb_upstream_create(config, ctx->ch_host, ctx->ch_port,
                                 io_flags, ins->tls);
    if (!ctx->u) {
        flb_plg_error(ins, "не создано соединение с ch %s:%i",
                      ctx->ch_host, ctx->ch_port);
        return -1;
    }
    flb_stream_disable_async_mode(&ctx->u->base);

    ret = flb_log_event_encoder_init(&ctx->log_encoder,
                                     FLB_LOG_EVENT_FORMAT_DEFAULT);
    if (ret != FLB_EVENT_ENCODER_SUCCESS) {
        flb_plg_error(ins, "кодировщик записей не поднялся: %i", ret);
        return -1;
    }

    if (dbm_schedule_init(&ctx->schedule, ctx->schedule_conf, ins,
                          ctx->interval_sec) != 0) {
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

    flb_plg_info(ins, "pg %s:%s → ch %s:%i %s, каждые %i с, батч %zu байт",
                 ctx->pg_host, ctx->pg_port, ctx->ch_host, ctx->ch_port,
                 ctx->ch_table, ctx->interval_sec, ctx->batch_bytes);
    dbm_schedule_log(&ctx->schedule, ins, ctx->schedule_conf);
    return 0;
}

static void cb_pause(void *data, struct flb_config *config)
{
    struct flb_in_pg2ch *ctx = data;
    (void) config;
    if (ctx->coll_fd >= 0) {
        flb_input_collector_pause(ctx->coll_fd, ctx->ins);
    }
}

static void cb_resume(void *data, struct flb_config *config)
{
    struct flb_in_pg2ch *ctx = data;
    (void) config;
    if (ctx->coll_fd >= 0) {
        flb_input_collector_resume(ctx->coll_fd, ctx->ins);
    }
}

static int cb_exit(void *data, struct flb_config *config)
{
    struct flb_in_pg2ch *ctx = data;

    (void) config;

    if (!ctx) {
        return 0;
    }
    if (ctx->conn) {
        PQfinish(ctx->conn);
    }
    if (ctx->u) {
        flb_upstream_destroy(ctx->u);
    }
    if (ctx->coll_fd >= 0) {
        flb_log_event_encoder_destroy(&ctx->log_encoder);
    }
    flb_sds_destroy(ctx->sql);
    flb_sds_destroy(ctx->pg_host);
    flb_sds_destroy(ctx->pg_port);
    flb_sds_destroy(ctx->ch_host);
    flb_sds_destroy(ctx->insert_uri);
    flb_free(ctx);
    return 0;
}

static struct flb_config_map config_map[] = {
    {
     FLB_CONFIG_MAP_STR, "pg_target", NULL,
     0, FLB_TRUE, offsetof(struct flb_in_pg2ch, pg_target),
     "Сервер PostgreSQL: host[:port]"
    },
    {
     FLB_CONFIG_MAP_STR, "pg_user", NULL,
     0, FLB_TRUE, offsetof(struct flb_in_pg2ch, pg_user),
     "Пользователь; при Kerberos имя берётся из билета и параметр не нужен"
    },
    {
     FLB_CONFIG_MAP_STR, "pg_password", NULL,
     0, FLB_TRUE, offsetof(struct flb_in_pg2ch, pg_password),
     "Пароль (при Kerberos не нужен)"
    },
    {
     FLB_CONFIG_MAP_STR, "pg_database", NULL,
     0, FLB_TRUE, offsetof(struct flb_in_pg2ch, pg_database),
     "База-источник"
    },
    {
     FLB_CONFIG_MAP_STR, "pg_options", NULL,
     0, FLB_TRUE, offsetof(struct flb_in_pg2ch, pg_options),
     "Довесок к строке подключения libpq, например sslmode=require"
    },
    {
     FLB_CONFIG_MAP_STR, "query", NULL,
     0, FLB_TRUE, offsetof(struct flb_in_pg2ch, query),
     "SELECT для COPY; {CURSOR} заменяется отметкой; ORDER BY по ней обязателен"
    },
    {
     FLB_CONFIG_MAP_STR, "query_file", NULL,
     0, FLB_TRUE, offsetof(struct flb_in_pg2ch, query_file),
     "Файл с запросом (важнее, чем query)"
    },
    {
     FLB_CONFIG_MAP_STR, "ch_target", NULL,
     0, FLB_TRUE, offsetof(struct flb_in_pg2ch, ch_target),
     "Нода ClickHouse: host[:port]"
    },
    {
     FLB_CONFIG_MAP_STR, "ch_user", NULL,
     0, FLB_TRUE, offsetof(struct flb_in_pg2ch, ch_user),
     "Пользователь ClickHouse"
    },
    {
     FLB_CONFIG_MAP_STR, "ch_password", NULL,
     0, FLB_TRUE, offsetof(struct flb_in_pg2ch, ch_password),
     "Пароль ClickHouse"
    },
    {
     FLB_CONFIG_MAP_STR, "ch_database", NULL,
     0, FLB_TRUE, offsetof(struct flb_in_pg2ch, ch_database),
     "База-приёмник"
    },
    {
     FLB_CONFIG_MAP_STR, "ch_table", NULL,
     0, FLB_TRUE, offsetof(struct flb_in_pg2ch, ch_table),
     "Целевая таблица"
    },
    {
     FLB_CONFIG_MAP_STR, "ch_columns", NULL,
     0, FLB_TRUE, offsetof(struct flb_in_pg2ch, ch_columns),
     "Список столбцов вставки через запятую — в порядке SELECT"
    },
    {
     FLB_CONFIG_MAP_STR, "ch_settings", "date_time_input_format=best_effort",
     0, FLB_TRUE, offsetof(struct flb_in_pg2ch, ch_settings),
     "Настройки CH для вставки, key=value через запятую"
    },
    {
     FLB_CONFIG_MAP_STR, "cursor_query", NULL,
     0, FLB_TRUE, offsetof(struct flb_in_pg2ch, cursor_query),
     "SQL к CH за отметкой (обычно SELECT max(...) FROM цель) → {CURSOR}"
    },
    {
     FLB_CONFIG_MAP_STR, "cursor_default", "0",
     0, FLB_TRUE, offsetof(struct flb_in_pg2ch, cursor_default),
     "Чем подставить {CURSOR}, если отметки нет (пустая цель)"
    },
    {
     FLB_CONFIG_MAP_SIZE, "batch_bytes", "64M",
     0, FLB_TRUE, offsetof(struct flb_in_pg2ch, batch_bytes),
     "Порог отправки батча в CH; каждая вставка — отдельный кусок MergeTree"
    },
    {
     FLB_CONFIG_MAP_BOOL, "report_empty", "off",
     0, FLB_TRUE, offsetof(struct flb_in_pg2ch, report_empty),
     "Писать запись-отчёт и о пустых прогонах"
    },
    {
     FLB_CONFIG_MAP_INT, "connect_timeout", "5",
     0, FLB_TRUE, offsetof(struct flb_in_pg2ch, connect_timeout),
     "Таймаут подключения к PG, секунды"
    },
    {
     FLB_CONFIG_MAP_INT, "statement_timeout", "0",
     0, FLB_TRUE, offsetof(struct flb_in_pg2ch, statement_timeout),
     "statement_timeout сеанса PG, миллисекунды; 0 — без ограничения"
    },
    {
     FLB_CONFIG_MAP_INT, "retry_pause_sec", "30",
     0, FLB_TRUE, offsetof(struct flb_in_pg2ch, retry_pause_sec),
     "Пауза перед следующей попыткой после неудачного подключения"
    },
    {
     FLB_CONFIG_MAP_STR, "schedule", NULL,
     0, FLB_TRUE, offsetof(struct flb_in_pg2ch, schedule_conf),
     "cron-выражение: прогон только в совпавшие моменты. 5 полей как в cron "
     "или 6 с секундами; часовой пояс — из TZ контейнера"
    },
    {
     FLB_CONFIG_MAP_INT, "interval_sec", DEFAULT_INTERVAL_SEC,
     0, FLB_TRUE, offsetof(struct flb_in_pg2ch, interval_sec),
     "Период прогонов, секунды"
    },
    {
     FLB_CONFIG_MAP_INT, "interval_nsec", "0",
     0, FLB_TRUE, offsetof(struct flb_in_pg2ch, interval_nsec),
     "Период прогонов, наносекунды"
    },
    {0}
};

struct flb_input_plugin in_pg2ch_plugin = {
    .name         = "pg2ch",
    .description  = "PostgreSQL COPY → ClickHouse INSERT (TabSeparated) pump",
    .cb_init      = cb_init,
    .cb_pre_run   = NULL,
    .cb_collect   = cb_collect,
    .cb_flush_buf = NULL,
    .cb_pause     = cb_pause,
    .cb_resume    = cb_resume,
    .cb_exit      = cb_exit,
    .config_map   = config_map,
    /* перегон большой таблицы — минуты: вход обязан жить в своём потоке,
     * иначе на время COPY встал бы весь конвейер */
    .flags        = FLB_INPUT_NET | FLB_INPUT_THREADED,
};
