/* dbmetrics.h — общее для входных плагинов, читающих ответ СУБД: режим работы,
 * списки имён из конфигурации, разбор значения в число и во время, набор gauge
 * на один цикл опроса.
 *
 * Только заголовок со static-функциями: каждый плагин — отдельная .so, общего
 * объектного файла между ними нет и линковать его неоткуда.
 *
 * Символы cmetrics (cmt_*) и flb_input_metrics_append берутся из самого
 * fluent-bit: он собран с ENABLE_EXPORTS, из .so они видны.
 */

#ifndef DBMETRICS_H
#define DBMETRICS_H

#include <fluent-bit/flb_input_plugin.h>
#include <fluent-bit/flb_input_metric.h>
#include <fluent-bit/flb_sds.h>
#include <fluent-bit/flb_time.h>

#include <cmetrics/cmetrics.h>
#include <cmetrics/cmt_gauge.h>

#include "ccronexpr.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* Что плагин кладёт в конвейер */
#define DBM_MODE_LOGS     0     /* записи (как было) */
#define DBM_MODE_METRICS  1     /* метрики cmetrics */
#define DBM_MODE_BOTH     2     /* и то, и другое */

/* Меток на метрику: instance + Label_Fields. Больше десятка меток в
 * prometheus — уже ошибка проектирования, но запас берём */
#define DBM_LABELS_MAX    16

/* Значение метки: длиннее — обрезается. Метка с текстом на сотню символов
 * означает, что в метки попало то, чему место в записи, а не в метрике */
#define DBM_LABEL_MAX     128

/* Как подставлять значение курсора в {CURSOR} */
#define DBM_CURSOR_STRING     0     /* 'значение' */
#define DBM_CURSOR_NUMBER     1     /* значение без кавычек */
#define DBM_CURSOR_DATETIME64 2     /* toDateTime64('значение', 6) — clickhouse */
#define DBM_CURSOR_RAW        3     /* как есть, подставляет автор запроса */

/* ------------------------------------------------------------------ режимы */

static int dbm_mode_parse(const char *value)
{
    if (!value || *value == '\0' || strcasecmp(value, "logs") == 0) {
        return DBM_MODE_LOGS;
    }
    if (strcasecmp(value, "metrics") == 0) {
        return DBM_MODE_METRICS;
    }
    if (strcasecmp(value, "both") == 0) {
        return DBM_MODE_BOTH;
    }
    return -1;
}

static int dbm_cursor_type_parse(const char *value)
{
    if (!value || *value == '\0' || strcasecmp(value, "string") == 0) {
        return DBM_CURSOR_STRING;
    }
    if (strcasecmp(value, "number") == 0) {
        return DBM_CURSOR_NUMBER;
    }
    if (strcasecmp(value, "datetime64") == 0) {
        return DBM_CURSOR_DATETIME64;
    }
    if (strcasecmp(value, "raw") == 0) {
        return DBM_CURSOR_RAW;
    }
    return -1;
}

/* ------------------------------------------------------------- расписание */

/* Разбор cron-выражений — библиотека ccronexpr (Apache 2.0), качается в
 * ci-artifacts по тегу из DEPS и компилируется рядом с плагином. Собрана с
 * CRON_USE_LOCAL_TIME, поэтому расписание считается в часовом поясе
 * контейнера: задаётся переменной TZ, по умолчанию UTC.
 *
 * Как это работает: коллектор продолжает тикать по Interval_Sec, а расписание
 * решает, делать ли прогон на этом тике. Тик должен быть не реже самой частой
 * минуты расписания — иначе момент можно проспать; плагин это проверяет и
 * предупреждает. */

struct dbm_schedule {
    int enabled;
    cron_expr expr;
    time_t next;                /* когда ближайший разрешённый прогон */
};

/* Библиотека требует ровно 6 полей (первое — секунды). Привычные пять
 * дополняем нулевой секундой сами: пользователь пишет "0 2 * * *", как в cron. */
static int dbm_schedule_init(struct dbm_schedule *sc, const char *expr,
                             struct flb_input_instance *ins, int interval_sec)
{
    const char *err = NULL;
    char buf[256];
    const char *p;
    int fields = 0;
    int in_field = 0;

    memset(sc, 0, sizeof(*sc));
    if (!expr || *expr == '\0') {
        return 0;
    }

    for (p = expr; *p; p++) {
        if (*p == ' ' || *p == '\t') {
            in_field = 0;
        }
        else if (!in_field) {
            in_field = 1;
            fields++;
        }
    }

    if (fields == 5) {
        if (snprintf(buf, sizeof(buf), "0 %s", expr) >= (int) sizeof(buf)) {
            flb_plg_error(ins, "schedule слишком длинное");
            return -1;
        }
    }
    else if (fields == 6) {
        if (snprintf(buf, sizeof(buf), "%s", expr) >= (int) sizeof(buf)) {
            flb_plg_error(ins, "schedule слишком длинное");
            return -1;
        }
    }
    else {
        flb_plg_error(ins, "schedule: нужно 5 полей (как в cron) или 6 "
                      "(с секундами), а их %i: %s", fields, expr);
        return -1;
    }

    cron_parse_expr(buf, &sc->expr, &err);
    if (err) {
        flb_plg_error(ins, "schedule не разобрано (%s): %s", err, expr);
        return -1;
    }

    /* тик реже минуты может перепрыгнуть разрешённый момент */
    if (interval_sec > 60) {
        flb_plg_warn(ins, "interval_sec %i больше минуты: моменты расписания "
                     "можно проспать, поставьте 60 или меньше", interval_sec);
    }

    sc->enabled = 1;
    sc->next = cron_next(&sc->expr, time(NULL));
    return 0;
}

/* Пора ли работать. Вызывается на каждом тике коллектора. */
static int dbm_schedule_due(struct dbm_schedule *sc, time_t now)
{
    if (!sc->enabled) {
        return FLB_TRUE;
    }
    if (now < sc->next) {
        return FLB_FALSE;
    }
    /* момент наступил: считаем следующий от текущего времени, а не от
     * пропущенного — иначе после долгой паузы плагин отработал бы подряд
     * столько раз, сколько моментов проспал */
    sc->next = cron_next(&sc->expr, now);
    return FLB_TRUE;
}

static void dbm_schedule_log(struct dbm_schedule *sc,
                             struct flb_input_instance *ins, const char *expr)
{
    struct tm tm;
    char when[64];

    if (!sc->enabled) {
        return;
    }
    localtime_r(&sc->next, &tm);
    strftime(when, sizeof(when), "%Y-%m-%d %H:%M:%S %Z", &tm);
    flb_plg_info(ins, "расписание «%s», ближайший прогон %s", expr, when);
}

/* --------------------------------------------------------------- имена полей */

/* Список имён столбцов из строки "a, b, c". Владеет одной копией строки,
 * item[] показывает внутрь неё. */
struct dbm_names {
    int count;
    char **item;
    char *copy;
};

static void dbm_names_destroy(struct dbm_names *names)
{
    if (names->item) {
        flb_free(names->item);
        names->item = NULL;
    }
    if (names->copy) {
        flb_free(names->copy);
        names->copy = NULL;
    }
    names->count = 0;
}

static int dbm_names_init(struct dbm_names *names, const char *csv)
{
    char *save = NULL;
    char *item;
    char **grown;

    memset(names, 0, sizeof(*names));
    if (!csv || *csv == '\0') {
        return 0;
    }

    names->copy = flb_strdup(csv);
    if (!names->copy) {
        return -1;
    }

    item = strtok_r(names->copy, ", \t", &save);
    while (item) {
        grown = flb_realloc(names->item, sizeof(char *) * (names->count + 1));
        if (!grown) {
            dbm_names_destroy(names);
            return -1;
        }
        names->item = grown;
        names->item[names->count++] = item;
        item = strtok_r(NULL, ", \t", &save);
    }
    return 0;
}

/* Позиция имени в списке или -1. Позиция важна: у меток порядок значений
 * должен совпадать с порядком ключей, объявленных при создании метрики. */
static int dbm_names_index(struct dbm_names *names,
                           const char *name, size_t name_len)
{
    int i;

    for (i = 0; i < names->count; i++) {
        if (strlen(names->item[i]) == name_len &&
            strncmp(names->item[i], name, name_len) == 0) {
            return i;
        }
    }
    return -1;
}

static int dbm_names_has(struct dbm_names *names,
                         const char *name, size_t name_len)
{
    return dbm_names_index(names, name, name_len) >= 0;
}

/* ------------------------------------------------------------------ значения */

/* Число целиком, а не «начинается с цифры»: strtod останавливается на первом
 * непонятном символе, поэтому "8123 rows" стал бы метрикой 8123.
 * Нужно и для clickhouse: в JSONEachRow 64-битные целые приезжают строками. */
static int dbm_number(const char *raw, size_t len, double *out)
{
    char buf[64];
    char *end = NULL;
    double value;

    if (len == 0 || len >= sizeof(buf)) {
        return -1;
    }
    memcpy(buf, raw, len);
    buf[len] = '\0';

    errno = 0;
    value = strtod(buf, &end);
    if (end == buf || errno == ERANGE) {
        return -1;
    }
    while (*end == ' ') {
        end++;
    }
    if (*end != '\0') {
        return -1;
    }

    *out = value;
    return 0;
}

/* Время события из столбца. Форматы, которые реально приезжают:
 *   2026-08-24 12:00:00[.123456][+03|+03:00|Z]  — postgres timestamptz, clickhouse
 *   2026-08-24T12:00:00[.123456][Z]             — ISO-8601
 *   1756036800[.123456]                          — epoch, если так задано в запросе
 * Без смещения время считается UTC: в запросах его и надо приводить к UTC,
 * иначе получатель не сойдётся с соседним источником. */
static int dbm_time(const char *raw, size_t len, struct flb_time *out)
{
    char buf[64];
    struct tm tm;
    const char *p;
    char *end;
    double epoch;
    long nsec = 0;
    long offset = 0;
    int sign = 1;
    int digits;

    if (len == 0 || len >= sizeof(buf)) {
        return -1;
    }
    memcpy(buf, raw, len);
    buf[len] = '\0';

    /* epoch: только цифры (и, может быть, дробная часть) */
    if (buf[0] >= '0' && buf[0] <= '9' && !strchr(buf, '-')) {
        if (dbm_number(buf, strlen(buf), &epoch) == 0) {
            flb_time_from_double(out, epoch);
            return 0;
        }
        return -1;
    }

    memset(&tm, 0, sizeof(tm));
    p = strptime(buf, "%Y-%m-%d %H:%M:%S", &tm);
    if (!p) {
        p = strptime(buf, "%Y-%m-%dT%H:%M:%S", &tm);
    }
    if (!p) {
        return -1;
    }

    /* дробная часть: сколько знаков дали, столько и берём */
    if (*p == '.') {
        p++;
        digits = 0;
        while (*p >= '0' && *p <= '9' && digits < 9) {
            nsec = nsec * 10 + (*p - '0');
            p++;
            digits++;
        }
        while (digits < 9) {
            nsec *= 10;
            digits++;
        }
        while (*p >= '0' && *p <= '9') {
            p++;
        }
    }

    /* смещение зоны */
    if (*p == '+' || *p == '-') {
        sign = (*p == '-') ? -1 : 1;
        p++;
        offset = strtol(p, &end, 10) * 3600;
        if (end && *end == ':') {
            offset += strtol(end + 1, NULL, 10) * 60;
        }
        else if (end && (end - p) == 4) {
            /* +0300 — минуты приклеены к часам */
            offset = (offset / 10000) * 3600 + ((offset / 100) % 100) * 60;
        }
    }

    out->tm.tv_sec = timegm(&tm) - sign * offset;
    out->tm.tv_nsec = nsec;
    return 0;
}

/* --------------------------------------------------------------- имя метрики */

/* Prometheus принимает [a-zA-Z_:][a-zA-Z0-9_:]*, остальное заменяем на '_'.
 * Иначе столбец вида "size(bytes)" молча теряется у экспортёра. */
static void dbm_metric_name(const char *raw, size_t len,
                            char *out, size_t out_size)
{
    size_t i;
    size_t n = 0;

    for (i = 0; i < len && n + 1 < out_size; i++) {
        char c = raw[i];
        if (isalnum((unsigned char) c) || c == '_' || c == ':') {
            out[n++] = (char) tolower((unsigned char) c);
        }
        else {
            out[n++] = '_';
        }
    }
    out[n] = '\0';
    if (n > 0 && out[0] >= '0' && out[0] <= '9' && out_size > n + 1) {
        memmove(out + 1, out, n + 1);
        out[0] = '_';
    }
}

/* ------------------------------------------------------- набор метрик цикла */

struct dbm_metric {
    flb_sds_t name;
    struct cmt_gauge *gauge;
    struct mk_list _head;
};

/* Один набор на цикл опроса: cmt живёт до flb_input_metrics_append, метрики
 * заводятся лениво — какие столбцы пришли, такие и появились. Один и тот же
 * gauge получает значения от всех серверов, они различаются меткой instance. */
struct dbm_set {
    struct cmt *cmt;
    struct mk_list metrics;
    char *label_key[DBM_LABELS_MAX];
    char *label_val[DBM_LABELS_MAX];
    char  label_buf[DBM_LABELS_MAX][DBM_LABEL_MAX];
    int label_count;
    int label_base;     /* 0 или 1: занята ли первая метка под instance */
};

/* instance_key может быть пустым — тогда метки instance не будет */
static int dbm_set_init(struct dbm_set *set, const char *instance_key,
                        struct dbm_names *labels)
{
    int i;

    memset(set, 0, sizeof(*set));
    mk_list_init(&set->metrics);

    set->cmt = cmt_create();
    if (!set->cmt) {
        return -1;
    }

    if (instance_key && *instance_key != '\0') {
        set->label_key[set->label_count++] = (char *) instance_key;
        set->label_base = 1;
    }
    for (i = 0; i < labels->count && set->label_count < DBM_LABELS_MAX; i++) {
        set->label_key[set->label_count++] = labels->item[i];
    }
    return 0;
}

static void dbm_set_destroy(struct dbm_set *set)
{
    struct mk_list *head;
    struct mk_list *tmp;
    struct dbm_metric *m;

    mk_list_foreach_safe(head, tmp, &set->metrics) {
        m = mk_list_entry(head, struct dbm_metric, _head);
        mk_list_del(&m->_head);
        flb_sds_destroy(m->name);
        flb_free(m);
    }
    if (set->cmt) {
        cmt_destroy(set->cmt);
        set->cmt = NULL;
    }
}

/* Значения меток на текущую строку. Порядок — как у ключей; чего в строке нет,
 * то остаётся пустой строкой: у метрики набор меток фиксирован при создании,
 * пропустить одну нельзя.
 *
 * Значения копируются в набор: у clickhouse они показывают внутрь msgpack и
 * не заканчиваются нулём, а cmt_gauge_set ждёт обычные строки. */
static void dbm_set_row_begin(struct dbm_set *set, const char *instance)
{
    int i;

    for (i = 0; i < set->label_count; i++) {
        set->label_buf[i][0] = '\0';
        set->label_val[i] = set->label_buf[i];
    }
    if (instance && set->label_base) {
        snprintf(set->label_buf[0], DBM_LABEL_MAX, "%s", instance);
    }
}

/* index — позиция в Label_Fields, метка instance учитывается сама */
static void dbm_set_label(struct dbm_set *set, int index,
                          const char *value, size_t len)
{
    int slot = set->label_base + index;

    if (slot < 0 || slot >= set->label_count) {
        return;
    }
    if (len >= DBM_LABEL_MAX) {
        len = DBM_LABEL_MAX - 1;
    }
    memcpy(set->label_buf[slot], value, len);
    set->label_buf[slot][len] = '\0';
}

static struct cmt_gauge *dbm_gauge(struct dbm_set *set,
                                   struct flb_input_instance *ins,
                                   const char *prefix,
                                   const char *column, size_t column_len)
{
    struct mk_list *head;
    struct dbm_metric *m;
    char name[192];

    dbm_metric_name(column, column_len, name, sizeof(name));

    mk_list_foreach(head, &set->metrics) {
        m = mk_list_entry(head, struct dbm_metric, _head);
        if (strcmp(m->name, name) == 0) {
            return m->gauge;
        }
    }

    m = flb_calloc(1, sizeof(struct dbm_metric));
    if (!m) {
        return NULL;
    }
    m->name = flb_sds_create(name);
    if (!m->name) {
        flb_free(m);
        return NULL;
    }

    /* subsystem — пустая строка, а не NULL: на NULL cmt_gauge_create молча
     * отказывает («null subsystem not allowed»). Пустым нельзя оставить и help:
     * prometheus-экспортёр печатает строку HELP всегда, а имя столбца —
     * единственное осмысленное описание, которое есть */
    m->gauge = cmt_gauge_create(set->cmt, (char *) prefix, "", name, name,
                                set->label_count, set->label_key);
    if (!m->gauge) {
        flb_plg_error(ins, "метрика %s_%s не создана", prefix, name);
        flb_sds_destroy(m->name);
        flb_free(m);
        return NULL;
    }

    mk_list_add(&m->_head, &set->metrics);
    return m->gauge;
}

static int dbm_set_value(struct dbm_set *set, struct flb_input_instance *ins,
                         const char *prefix, const char *column,
                         size_t column_len, double value, uint64_t ts)
{
    struct cmt_gauge *g;

    g = dbm_gauge(set, ins, prefix, column, column_len);
    if (!g) {
        return -1;
    }
    return cmt_gauge_set(g, ts, value, set->label_count, set->label_val);
}

#endif
