/* out_postgres — выходной плагин: пачка событий уезжает в PostgreSQL одним
 * запросом, а что с ней делать, описывает произвольный SQL.
 *
 * Пачка передаётся ПАРАМЕТРОМ $1 типа jsonb (массив объектов), поэтому в текст
 * запроса значения не попадают вовсе — инъекции взяться неоткуда, экранировать
 * нечего. Разворачивают массив штатные функции постгреса:
 *
 *   INSERT INTO t SELECT * FROM jsonb_populate_recordset(null::t, $1)
 *   INSERT INTO agg SELECT host, count(*) FROM jsonb_to_recordset($1)
 *          AS x(host text, bytes bigint) GROUP BY host
 *
 * Table — короткая запись первого варианта: плагин сам соберёт
 * jsonb_populate_recordset по имени таблицы.
 *
 * libpq не линкуется: она в бинарнике ради out_pgsql, ENABLE_EXPORTS делает
 * PQ* видимыми из .so — оттуда же GSSAPI/Kerberos.
 *
 * Загрузчик ищет в .so структуру `out_postgres_plugin`.
 */

#include <fluent-bit/flb_output_plugin.h>
#include <fluent-bit/flb_config_map.h>
#include <fluent-bit/flb_sds.h>

#include <libpq-fe.h>

#include "dbsink.h"

#define DEFAULT_PORT "5432"

struct flb_out_postgres {
    struct flb_output_instance *ins;

    /* конфигурация */
    flb_sds_t host;
    flb_sds_t port;
    flb_sds_t user;
    flb_sds_t password;
    flb_sds_t database;
    flb_sds_t conn_options;
    flb_sds_t table;
    flb_sds_t query;
    flb_sds_t query_file;
    flb_sds_t time_key;
    flb_sds_t time_format_conf;
    int connect_timeout;
    int statement_timeout;

    int time_format;
    flb_sds_t sql;          /* готовый запрос с $1 */
    PGconn *conn;
};

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

static int pg_connect(struct flb_out_postgres *ctx)
{
    flb_sds_t conninfo;
    char timeout[32];
    char options[64];

    if (ctx->conn) {
        if (PQstatus(ctx->conn) == CONNECTION_OK) {
            return 0;
        }
        PQreset(ctx->conn);
        if (PQstatus(ctx->conn) == CONNECTION_OK) {
            flb_plg_info(ctx->ins, "%s:%s — соединение восстановлено",
                         ctx->host, ctx->port);
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
    conninfo_add(&conninfo, "host", ctx->host);
    conninfo_add(&conninfo, "port", ctx->port);
    conninfo_add(&conninfo, "connect_timeout", timeout);
    conninfo_add(&conninfo, "options", options);
    conninfo_add(&conninfo, "application_name", "fluent-bit out_postgres");
    if (ctx->database && flb_sds_len(ctx->database) > 0) {
        conninfo_add(&conninfo, "dbname", ctx->database);
    }
    if (ctx->user && flb_sds_len(ctx->user) > 0) {
        conninfo_add(&conninfo, "user", ctx->user);
    }
    if (ctx->password && flb_sds_len(ctx->password) > 0) {
        conninfo_add(&conninfo, "password", ctx->password);
    }
    if (ctx->conn_options && flb_sds_len(ctx->conn_options) > 0) {
        flb_sds_printf(&conninfo, "%s", ctx->conn_options);
    }

    ctx->conn = PQconnectdb(conninfo);
    flb_sds_destroy(conninfo);

    if (!ctx->conn || PQstatus(ctx->conn) != CONNECTION_OK) {
        flb_plg_error(ctx->ins, "%s:%s — не подключиться: %s",
                      ctx->host, ctx->port,
                      ctx->conn ? PQerrorMessage(ctx->conn) : "нет памяти");
        if (ctx->conn) {
            PQfinish(ctx->conn);
            ctx->conn = NULL;
        }
        return -1;
    }

    flb_plg_info(ctx->ins, "%s:%s — подключён (%s)", ctx->host, ctx->port,
                 PQparameterStatus(ctx->conn, "server_version"));
    return 0;
}

static int cb_init(struct flb_output_instance *ins, struct flb_config *config,
                   void *data)
{
    struct flb_out_postgres *ctx;
    const char *tmp;
    char port[16];
    int ret;

    (void) data;
    (void) config;

    ctx = flb_calloc(1, sizeof(struct flb_out_postgres));
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

    /* у сетевых выходов (FLB_OUTPUT_NET) ядро разбирает Host/Port само и
     * кладёт в ins->host — в списке свойств их уже нет */
    tmp = ins->host.name;
    ctx->host = flb_sds_create(tmp ? tmp : "127.0.0.1");
    if (ins->host.port > 0) {
        snprintf(port, sizeof(port), "%i", ins->host.port);
        ctx->port = flb_sds_create(port);
    }
    else {
        ctx->port = flb_sds_create(DEFAULT_PORT);
    }

    if (ctx->query_file && flb_sds_len(ctx->query_file) > 0) {
        ctx->sql = read_file(ins, ctx->query_file);
    }
    else if (ctx->query && flb_sds_len(ctx->query) > 0) {
        ctx->sql = flb_sds_create_len(ctx->query, flb_sds_len(ctx->query));
    }
    else if (ctx->table && flb_sds_len(ctx->table) > 0) {
        /* поля записи ложатся в одноимённые столбцы, лишние отбрасываются —
         * этим занимается сам jsonb_populate_recordset */
        ctx->sql = flb_sds_create_size(128);
        if (ctx->sql) {
            flb_sds_printf(&ctx->sql,
                           "INSERT INTO %s SELECT * FROM "
                           "jsonb_populate_recordset(null::%s, $1)",
                           ctx->table, ctx->table);
        }
    }
    else {
        flb_plg_error(ins, "нужен table, query или query_file");
        return -1;
    }
    if (!ctx->sql) {
        return -1;
    }

    if (!strstr(ctx->sql, "$1")) {
        flb_plg_error(ins, "в запросе нет $1 — пачку некуда подставить");
        return -1;
    }

    flb_plg_info(ins, "%s:%s, время в поле %s (%s)", ctx->host, ctx->port,
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
    struct flb_out_postgres *ctx = out_context;
    flb_sds_t body;
    const char *values[1];
    PGresult *res;
    ExecStatusType status;
    const char *sqlstate;
    int rows;
    int retry = FLB_FALSE;

    (void) i_ins;
    (void) config;

    body = flb_sds_create_size(4096);
    if (!body) {
        FLB_OUTPUT_RETURN(FLB_RETRY);
    }
    flb_sds_cat_safe(&body, "[", 1);

    rows = dbsink_chunk_json(event_chunk, ctx->time_key, ctx->time_format,
                             ",", &body);
    if (rows < 0) {
        flb_plg_error(ctx->ins, "пачка событий не разобрана");
        flb_sds_destroy(body);
        FLB_OUTPUT_RETURN(FLB_ERROR);
    }
    flb_sds_cat_safe(&body, "]", 1);

    if (rows == 0) {
        flb_sds_destroy(body);
        FLB_OUTPUT_RETURN(FLB_OK);
    }

    if (pg_connect(ctx) != 0) {
        flb_sds_destroy(body);
        FLB_OUTPUT_RETURN(FLB_RETRY);
    }

    /* один параметр — весь батч. Текст запроса не меняется от вызова к вызову,
     * поэтому серверу он приезжает с уже разобранным планом */
    values[0] = body;
    res = PQexecParams(ctx->conn, ctx->sql, 1, NULL, values, NULL, NULL, 0);

    if (!res) {
        flb_plg_error(ctx->ins, "запрос не выполнен: %s", PQerrorMessage(ctx->conn));
        PQfinish(ctx->conn);
        ctx->conn = NULL;
        flb_sds_destroy(body);
        FLB_OUTPUT_RETURN(FLB_RETRY);
    }

    status = PQresultStatus(res);
    if (status != PGRES_COMMAND_OK && status != PGRES_TUPLES_OK) {
        sqlstate = PQresultErrorField(res, PG_DIAG_SQLSTATE);
        flb_plg_error(ctx->ins, "запрос не выполнен [%s]: %s",
                      sqlstate ? sqlstate : "?", PQresultErrorMessage(res));
        /* класс 08 (обрыв связи), 53 (нет ресурсов), 57 (вмешательство
         * оператора) проходят сами; ошибка в данных или в схеме — нет */
        if (sqlstate && (strncmp(sqlstate, "08", 2) == 0 ||
                         strncmp(sqlstate, "53", 2) == 0 ||
                         strncmp(sqlstate, "57", 2) == 0)) {
            retry = FLB_TRUE;
        }
        if (PQstatus(ctx->conn) != CONNECTION_OK) {
            PQfinish(ctx->conn);
            ctx->conn = NULL;
            retry = FLB_TRUE;
        }
        PQclear(res);
        flb_sds_destroy(body);
        FLB_OUTPUT_RETURN(retry ? FLB_RETRY : FLB_ERROR);
    }

    flb_plg_debug(ctx->ins, "записей %i, байт %zu, затронуто строк %s",
                  rows, flb_sds_len(body), PQcmdTuples(res));
    PQclear(res);
    flb_sds_destroy(body);
    FLB_OUTPUT_RETURN(FLB_OK);
}

static int cb_exit(void *data, struct flb_config *config)
{
    struct flb_out_postgres *ctx = data;

    (void) config;

    if (!ctx) {
        return 0;
    }
    if (ctx->conn) {
        PQfinish(ctx->conn);
    }
    flb_sds_destroy(ctx->host);
    flb_sds_destroy(ctx->port);
    flb_sds_destroy(ctx->sql);
    flb_free(ctx);
    return 0;
}

static struct flb_config_map config_map[] = {
    {
     FLB_CONFIG_MAP_STR, "user", NULL,
     0, FLB_TRUE, offsetof(struct flb_out_postgres, user),
     "Пользователь; при Kerberos имя берётся из билета и параметр не нужен"
    },
    {
     FLB_CONFIG_MAP_STR, "password", NULL,
     0, FLB_TRUE, offsetof(struct flb_out_postgres, password),
     "Пароль (при Kerberos не нужен)"
    },
    {
     FLB_CONFIG_MAP_STR, "database", NULL,
     0, FLB_TRUE, offsetof(struct flb_out_postgres, database),
     "База, к которой подключаться"
    },
    {
     FLB_CONFIG_MAP_STR, "conn_options", NULL,
     0, FLB_TRUE, offsetof(struct flb_out_postgres, conn_options),
     "Довесок к строке подключения libpq, например sslmode=require"
    },
    {
     FLB_CONFIG_MAP_STR, "table", NULL,
     0, FLB_TRUE, offsetof(struct flb_out_postgres, table),
     "Таблица для простой вставки: поля записи → одноимённые столбцы"
    },
    {
     FLB_CONFIG_MAP_STR, "query", NULL,
     0, FLB_TRUE, offsetof(struct flb_out_postgres, query),
     "Произвольный SQL; пачка приезжает параметром $1 типа jsonb"
    },
    {
     FLB_CONFIG_MAP_STR, "query_file", NULL,
     0, FLB_TRUE, offsetof(struct flb_out_postgres, query_file),
     "Файл с запросом (важнее, чем query и table)"
    },
    {
     FLB_CONFIG_MAP_STR, "time_key", "timestamp",
     0, FLB_TRUE, offsetof(struct flb_out_postgres, time_key),
     "Поле, куда положить время события; пустое — не добавлять"
    },
    {
     FLB_CONFIG_MAP_STR, "time_format", "iso8601",
     0, FLB_TRUE, offsetof(struct flb_out_postgres, time_format_conf),
     "Формат времени: iso8601, datetime64 или epoch"
    },
    {
     FLB_CONFIG_MAP_INT, "connect_timeout", "5",
     0, FLB_TRUE, offsetof(struct flb_out_postgres, connect_timeout),
     "Таймаут подключения, секунды"
    },
    {
     FLB_CONFIG_MAP_INT, "statement_timeout", "30000",
     0, FLB_TRUE, offsetof(struct flb_out_postgres, statement_timeout),
     "statement_timeout сеанса, миллисекунды"
    },
    {0}
};

struct flb_output_plugin out_postgres_plugin = {
    .name         = "postgres",
    .description  = "PostgreSQL output: записи и метрики произвольным SQL",
    .cb_init      = cb_init,
    .cb_flush     = cb_flush,
    .cb_exit      = cb_exit,
    .config_map   = config_map,
    .event_type   = FLB_OUTPUT_LOGS | FLB_OUTPUT_METRICS,
    .flags        = FLB_OUTPUT_NET,
};
