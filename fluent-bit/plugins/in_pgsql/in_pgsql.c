/* in_pgsql — входной плагин: выполняет SQL в PostgreSQL, строки результата
 * становятся записями.
 *
 * libpq не линкуется: она уже в бинарнике ради out_pgsql, а fluent-bit собран
 * с ENABLE_EXPORTS — символы PQ* видны из .so. Оттуда же берётся GSSAPI.
 * Типы столбцов сохраняются по OID из PQftype; {CURSOR} — инкрементальное чтение.
 */

#include <fluent-bit/flb_input_plugin.h>
#include <fluent-bit/flb_pack.h>
#include <fluent-bit/flb_time.h>
#include <fluent-bit/flb_config_map.h>
#include <fluent-bit/flb_sds.h>
#include <fluent-bit/flb_log_event_encoder.h>

#include <libpq-fe.h>

#define DEFAULT_INTERVAL_SEC  "5"
#define DEFAULT_PORT          "5432"
#define CURSOR_MAX            128

/* OID типов postgres. Берутся из pg_type и не меняются между версиями,
 * поэтому проще написать числа, чем тащить серверный catalog/pg_type.h */
#define OID_BOOL      16
#define OID_INT8      20
#define OID_INT2      21
#define OID_INT4      23
#define OID_JSON     114
#define OID_FLOAT4   700
#define OID_FLOAT8   701
#define OID_NUMERIC 1700
#define OID_JSONB   3802

/* Один сервер: соединение, его имя и его курсор. */
struct pg_target {
    flb_sds_t host;
    flb_sds_t port;
    PGconn *conn;
    char cursor[CURSOR_MAX];
    time_t retry_after;     /* пауза после неудачного подключения */
    struct mk_list _head;
};

struct flb_in_pgsql {
    struct flb_input_instance *ins;
    struct mk_list targets;

    /* конфигурация */
    flb_sds_t targets_conf;
    flb_sds_t user;
    flb_sds_t password;
    flb_sds_t database;
    flb_sds_t conn_options;
    flb_sds_t query;
    flb_sds_t query_file;
    flb_sds_t instance_field;
    flb_sds_t cursor_field;
    flb_sds_t cursor_default;
    flb_sds_t cursor_file;
    int connect_timeout;
    int statement_timeout;
    int retry_pause_sec;
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

/* Файл курсора сервера: <cursor_file>.<host>-<port>. Отдельный на сервер —
 * курсоры независимы, общий файл затирал бы соседний. */
static void cursor_path(struct flb_in_pgsql *ctx, struct pg_target *t,
                        char *out, size_t out_size)
{
    snprintf(out, out_size, "%s.%s-%s", ctx->cursor_file, t->host, t->port);
}

static void cursor_load(struct flb_in_pgsql *ctx, struct pg_target *t)
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
        flb_plg_info(ctx->ins, "%s:%s — курсор с прошлого запуска: %s",
                     t->host, t->port, t->cursor);
    }
}

static void cursor_save(struct flb_in_pgsql *ctx, struct pg_target *t)
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

/* "host:port,host2" → список серверов */
static int parse_targets(struct flb_in_pgsql *ctx)
{
    char *copy;
    char *save = NULL;
    char *item;
    char *colon;
    struct pg_target *t;

    copy = flb_strdup(ctx->targets_conf);
    if (!copy) {
        return -1;
    }

    item = strtok_r(copy, ", ", &save);
    while (item) {
        t = flb_calloc(1, sizeof(struct pg_target));
        if (!t) {
            flb_free(copy);
            return -1;
        }
        colon = strrchr(item, ':');
        if (colon) {
            *colon = '\0';
            t->port = flb_sds_create(colon + 1);
        }
        else {
            t->port = flb_sds_create(DEFAULT_PORT);
        }
        t->host = flb_sds_create(item);
        cursor_load(ctx, t);
        mk_list_add(&t->_head, &ctx->targets);
        flb_plg_info(ctx->ins, "сервер %s:%s", t->host, t->port);
        item = strtok_r(NULL, ", ", &save);
    }
    flb_free(copy);

    if (mk_list_size(&ctx->targets) == 0) {
        flb_plg_error(ctx->ins, "в targets нет ни одного сервера");
        return -1;
    }
    return 0;
}

/* Подстановка {CURSOR}: без курсора — значение по умолчанию из конфигурации. */
static flb_sds_t build_query(struct flb_in_pgsql *ctx, struct pg_target *t)
{
    flb_sds_t out;
    const char *p;
    const char *mark;
    flb_sds_t value;

    if (!strstr(ctx->sql, "{CURSOR}")) {
        return flb_sds_create_len(ctx->sql, flb_sds_len(ctx->sql));
    }

    if (t->cursor[0] != '\0') {
        value = flb_sds_create_size(CURSOR_MAX + 32);
        if (!value) {
            return NULL;
        }
        flb_sds_printf(&value, "'%s'", t->cursor);
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

/* -------------------------------------------------------------- соединение */

/* Значение параметра строки подключения: libpq снимает кавычки и обратные
 * слэши, поэтому в пароле они должны приехать удвоенными. */
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

static int connect_target(struct flb_in_pgsql *ctx, struct pg_target *t)
{
    flb_sds_t conninfo;
    char timeout[32];
    char options[64];

    /* сервер, который отказывает, не надо опрашивать в темпе Interval_Sec:
     * каждая попытка — ещё одно обращение к KDC */
    if (!t->conn && t->retry_after > time(NULL)) {
        return -1;
    }

    if (t->conn) {
        if (PQstatus(t->conn) == CONNECTION_OK) {
            return 0;
        }
        PQreset(t->conn);
        if (PQstatus(t->conn) == CONNECTION_OK) {
            flb_plg_info(ctx->ins, "%s:%s — соединение восстановлено", t->host, t->port);
            return 0;
        }
        PQfinish(t->conn);
        t->conn = NULL;
    }

    snprintf(timeout, sizeof(timeout), "%i", ctx->connect_timeout);
    snprintf(options, sizeof(options), "-c statement_timeout=%i", ctx->statement_timeout);

    conninfo = flb_sds_create_size(256);
    if (!conninfo) {
        return -1;
    }
    conninfo_add(&conninfo, "host", t->host);
    conninfo_add(&conninfo, "port", t->port);
    conninfo_add(&conninfo, "connect_timeout", timeout);
    conninfo_add(&conninfo, "options", options);
    conninfo_add(&conninfo, "application_name", "fluent-bit in_pgsql");
    if (ctx->database && flb_sds_len(ctx->database) > 0) {
        conninfo_add(&conninfo, "dbname", ctx->database);
    }
    if (ctx->user && flb_sds_len(ctx->user) > 0) {
        conninfo_add(&conninfo, "user", ctx->user);
    }
    if (ctx->password && flb_sds_len(ctx->password) > 0) {
        conninfo_add(&conninfo, "password", ctx->password);
    }
    /* conn_options идёт как есть: там пишут готовые пары libpq */
    if (ctx->conn_options && flb_sds_len(ctx->conn_options) > 0) {
        flb_sds_printf(&conninfo, "%s", ctx->conn_options);
    }

    t->conn = PQconnectdb(conninfo);
    flb_sds_destroy(conninfo);

    if (!t->conn || PQstatus(t->conn) != CONNECTION_OK) {
        flb_plg_error(ctx->ins, "%s:%s — не подключиться (следующая попытка через %i с): %s",
                      t->host, t->port, ctx->retry_pause_sec,
                      t->conn ? PQerrorMessage(t->conn) : "нет памяти");
        if (t->conn) {
            PQfinish(t->conn);
            t->conn = NULL;
        }
        t->retry_after = time(NULL) + ctx->retry_pause_sec;
        return -1;
    }

    t->retry_after = 0;
    flb_plg_info(ctx->ins, "%s:%s — подключён (%s)", t->host, t->port,
                 PQparameterStatus(t->conn, "server_version"));
    return 0;
}

/* ------------------------------------------------------------------- сбор */

/* Значение одной ячейки — своим типом, а не строкой. */
static int append_value(struct flb_in_pgsql *ctx, PGresult *res, int row, int col)
{
    char *raw;
    int len;
    Oid type;
    char *mp_buf = NULL;
    size_t mp_size = 0;
    int root_type;
    size_t consumed;
    int ret;

    if (PQgetisnull(res, row, col)) {
        return flb_log_event_encoder_append_null(&ctx->log_encoder,
                                                 FLB_LOG_EVENT_BODY);
    }

    raw = PQgetvalue(res, row, col);
    len = PQgetlength(res, row, col);
    type = PQftype(res, col);

    switch (type) {
    case OID_BOOL:
        return flb_log_event_encoder_append_boolean(&ctx->log_encoder,
                                                    FLB_LOG_EVENT_BODY,
                                                    raw[0] == 't');
    case OID_INT2:
    case OID_INT4:
    case OID_INT8:
        return flb_log_event_encoder_append_int64(&ctx->log_encoder,
                                                  FLB_LOG_EVENT_BODY,
                                                  strtoll(raw, NULL, 10));
    case OID_FLOAT4:
    case OID_FLOAT8:
    case OID_NUMERIC:
        return flb_log_event_encoder_append_double(&ctx->log_encoder,
                                                   FLB_LOG_EVENT_BODY,
                                                   strtod(raw, NULL));
    case OID_JSON:
    case OID_JSONB:
        /* json приезжает вложенным объектом: разбирать его на стороне
         * получателя пришлось бы вторым проходом */
        ret = flb_pack_json(raw, (size_t) len, &mp_buf, &mp_size,
                            &root_type, &consumed);
        if (ret == 0) {
            ret = flb_log_event_encoder_append_raw_msgpack(&ctx->log_encoder,
                                                           FLB_LOG_EVENT_BODY,
                                                           mp_buf, mp_size);
            flb_free(mp_buf);
            return ret;
        }
        /* не разобрался — отдаём как есть, лучше строка, чем потеря */
        break;
    default:
        break;
    }

    return flb_log_event_encoder_append_string(&ctx->log_encoder,
                                               FLB_LOG_EVENT_BODY,
                                               raw, (size_t) len);
}

static int emit_row(struct flb_in_pgsql *ctx, struct pg_target *t,
                    PGresult *res, int row, int cols)
{
    struct flb_time tm;
    int ret;
    int col;

    flb_time_get(&tm);
    ret = flb_log_event_encoder_begin_record(&ctx->log_encoder);
    if (ret == FLB_EVENT_ENCODER_SUCCESS) {
        ret = flb_log_event_encoder_set_timestamp(&ctx->log_encoder, &tm);
    }

    /* имя сервера добавляет плагин: у postgres нет своего hostName(), а
     * различать источники на той стороне надо */
    if (ret == FLB_EVENT_ENCODER_SUCCESS &&
        ctx->instance_field && flb_sds_len(ctx->instance_field) > 0) {
        ret = flb_log_event_encoder_append_string(&ctx->log_encoder,
                                                  FLB_LOG_EVENT_BODY,
                                                  ctx->instance_field,
                                                  flb_sds_len(ctx->instance_field));
        if (ret == FLB_EVENT_ENCODER_SUCCESS) {
            ret = flb_log_event_encoder_append_string(&ctx->log_encoder,
                                                      FLB_LOG_EVENT_BODY,
                                                      t->host, flb_sds_len(t->host));
        }
    }

    for (col = 0; col < cols && ret == FLB_EVENT_ENCODER_SUCCESS; col++) {
        ret = flb_log_event_encoder_append_cstring(&ctx->log_encoder,
                                                   FLB_LOG_EVENT_BODY,
                                                   PQfname(res, col));
        if (ret == FLB_EVENT_ENCODER_SUCCESS) {
            ret = append_value(ctx, res, row, col);
        }
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

static void collect_target(struct flb_in_pgsql *ctx, struct pg_target *t)
{
    flb_sds_t query;
    PGresult *res;
    int rows;
    int cols;
    int row;
    int cursor_col = -1;
    char newest[CURSOR_MAX];
    int emitted = 0;

    if (connect_target(ctx, t) != 0) {
        return;
    }

    query = build_query(ctx, t);
    if (!query) {
        return;
    }

    res = PQexec(t->conn, query);
    flb_sds_destroy(query);

    if (!res || PQresultStatus(res) != PGRES_TUPLES_OK) {
        flb_plg_error(ctx->ins, "%s:%s — запрос не выполнен: %s", t->host, t->port,
                      res ? PQresultErrorMessage(res) : PQerrorMessage(t->conn));
        if (res) {
            PQclear(res);
        }
        /* соединение могло умереть вместе с запросом — следующий сбор переподключится */
        if (PQstatus(t->conn) != CONNECTION_OK) {
            PQfinish(t->conn);
            t->conn = NULL;
        }
        return;
    }

    rows = PQntuples(res);
    cols = PQnfields(res);
    if (ctx->cursor_field && flb_sds_len(ctx->cursor_field) > 0) {
        cursor_col = PQfnumber(res, ctx->cursor_field);
    }
    newest[0] = '\0';

    for (row = 0; row < rows; row++) {
        if (emit_row(ctx, t, res, row, cols) != 0) {
            continue;
        }
        emitted++;
        if (cursor_col >= 0 && !PQgetisnull(res, row, cursor_col)) {
            const char *value = PQgetvalue(res, row, cursor_col);
            if (newest[0] == '\0' || strcmp(value, newest) > 0) {
                snprintf(newest, sizeof(newest), "%s", value);
            }
        }
    }

    if (emitted > 0) {
        flb_input_log_append(ctx->ins, NULL, 0,
                             ctx->log_encoder.output_buffer,
                             ctx->log_encoder.output_length);
        flb_log_event_encoder_reset(&ctx->log_encoder);
        if (newest[0] != '\0') {
            memcpy(t->cursor, newest, sizeof(newest));
            cursor_save(ctx, t);
        }
        flb_plg_debug(ctx->ins, "%s:%s — строк %i", t->host, t->port, emitted);
    }

    PQclear(res);
}

static int cb_collect(struct flb_input_instance *ins,
                      struct flb_config *config, void *in_context)
{
    struct flb_in_pgsql *ctx = in_context;
    struct mk_list *head;
    struct pg_target *t;

    (void) ins;
    (void) config;

    mk_list_foreach(head, &ctx->targets) {
        t = mk_list_entry(head, struct pg_target, _head);
        collect_target(ctx, t);
    }
    return 0;
}

/* -------------------------------------------------------------- жизненный цикл */

static int cb_init(struct flb_input_instance *ins,
                   struct flb_config *config, void *data)
{
    struct flb_in_pgsql *ctx;
    int ret;

    (void) data;

    ctx = flb_calloc(1, sizeof(struct flb_in_pgsql));
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

    ctx->coll_fd = -1;
    flb_input_set_context(ins, ctx);

    /* пустой список — не ошибка: так выключается запрос к представлению,
     * которого нет ни на одном из серверов этой установки */
    if (!ctx->targets_conf || flb_sds_len(ctx->targets_conf) == 0) {
        flb_plg_warn(ins, "серверов не задано — вход простаивает");
        return 0;
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

    if (parse_targets(ctx) != 0) {
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

    flb_plg_info(ins, "опрос каждые %i с, серверов %i",
                 ctx->interval_sec, mk_list_size(&ctx->targets));
    return 0;
}

static void cb_pause(void *data, struct flb_config *config)
{
    struct flb_in_pgsql *ctx = data;
    (void) config;
    if (ctx->coll_fd >= 0) {
        flb_input_collector_pause(ctx->coll_fd, ctx->ins);
    }
}

static void cb_resume(void *data, struct flb_config *config)
{
    struct flb_in_pgsql *ctx = data;
    (void) config;
    if (ctx->coll_fd >= 0) {
        flb_input_collector_resume(ctx->coll_fd, ctx->ins);
    }
}

static int cb_exit(void *data, struct flb_config *config)
{
    struct flb_in_pgsql *ctx = data;
    struct mk_list *head;
    struct mk_list *tmp;
    struct pg_target *t;

    (void) config;

    if (!ctx) {
        return 0;
    }

    mk_list_foreach_safe(head, tmp, &ctx->targets) {
        t = mk_list_entry(head, struct pg_target, _head);
        mk_list_del(&t->_head);
        if (t->conn) {
            PQfinish(t->conn);
        }
        flb_sds_destroy(t->host);
        flb_sds_destroy(t->port);
        flb_free(t);
    }

    if (ctx->coll_fd >= 0) {
        flb_log_event_encoder_destroy(&ctx->log_encoder);
    }
    if (ctx->sql) {
        flb_sds_destroy(ctx->sql);
    }
    flb_free(ctx);
    return 0;
}

static struct flb_config_map config_map[] = {
    {
     FLB_CONFIG_MAP_STR, "targets", NULL,
     0, FLB_TRUE, offsetof(struct flb_in_pgsql, targets_conf),
     "Серверы PostgreSQL: host[:port] через запятую"
    },
    {
     FLB_CONFIG_MAP_STR, "user", NULL,
     0, FLB_TRUE, offsetof(struct flb_in_pgsql, user),
     "Пользователь; при Kerberos имя берётся из билета и параметр не нужен"
    },
    {
     FLB_CONFIG_MAP_STR, "password", NULL,
     0, FLB_TRUE, offsetof(struct flb_in_pgsql, password),
     "Пароль (при Kerberos не нужен)"
    },
    {
     FLB_CONFIG_MAP_STR, "database", NULL,
     0, FLB_TRUE, offsetof(struct flb_in_pgsql, database),
     "База, к которой подключаться"
    },
    {
     FLB_CONFIG_MAP_STR, "conn_options", NULL,
     0, FLB_TRUE, offsetof(struct flb_in_pgsql, conn_options),
     "Довесок к строке подключения libpq, например sslmode=require gssencmode=require"
    },
    {
     FLB_CONFIG_MAP_STR, "query", NULL,
     0, FLB_TRUE, offsetof(struct flb_in_pgsql, query),
     "Текст SQL-запроса"
    },
    {
     FLB_CONFIG_MAP_STR, "query_file", NULL,
     0, FLB_TRUE, offsetof(struct flb_in_pgsql, query_file),
     "Файл с SQL-запросом (важнее, чем query)"
    },
    {
     FLB_CONFIG_MAP_STR, "instance_field", "instance",
     0, FLB_TRUE, offsetof(struct flb_in_pgsql, instance_field),
     "Имя поля, куда плагин кладёт имя сервера; пустое — не добавлять"
    },
    {
     FLB_CONFIG_MAP_STR, "cursor_field", NULL,
     0, FLB_TRUE, offsetof(struct flb_in_pgsql, cursor_field),
     "Столбец результата, из которого берётся курсор для {CURSOR}"
    },
    {
     FLB_CONFIG_MAP_STR, "cursor_default", "now() - INTERVAL '30 seconds'",
     0, FLB_TRUE, offsetof(struct flb_in_pgsql, cursor_default),
     "Чем подставить {CURSOR} на первом запросе"
    },
    {
     FLB_CONFIG_MAP_STR, "cursor_file", NULL,
     0, FLB_TRUE, offsetof(struct flb_in_pgsql, cursor_file),
     "Путь-основа для хранения курсора между перезапусками (на сервер свой файл)"
    },
    {
     FLB_CONFIG_MAP_INT, "connect_timeout", "5",
     0, FLB_TRUE, offsetof(struct flb_in_pgsql, connect_timeout),
     "Таймаут подключения, секунды"
    },
    {
     FLB_CONFIG_MAP_INT, "statement_timeout", "5000",
     0, FLB_TRUE, offsetof(struct flb_in_pgsql, statement_timeout),
     "statement_timeout сеанса, миллисекунды"
    },
    {
     FLB_CONFIG_MAP_INT, "retry_pause_sec", "30",
     0, FLB_TRUE, offsetof(struct flb_in_pgsql, retry_pause_sec),
     "Пауза перед следующей попыткой после неудачного подключения, секунды"
    },
    {
     FLB_CONFIG_MAP_INT, "interval_sec", DEFAULT_INTERVAL_SEC,
     0, FLB_TRUE, offsetof(struct flb_in_pgsql, interval_sec),
     "Период опроса, секунды"
    },
    {
     FLB_CONFIG_MAP_INT, "interval_nsec", "0",
     0, FLB_TRUE, offsetof(struct flb_in_pgsql, interval_nsec),
     "Период опроса, наносекунды"
    },
    {0}
};

struct flb_input_plugin in_pgsql_plugin = {
    .name         = "pgsql",
    .description  = "PostgreSQL SQL input (libpq)",
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
