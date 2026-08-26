/* dbsink.h — общее для выходных плагинов, которые кладут поток fluent-bit в
 * СУБД: пачка событий превращается в JSON, а дальше её разбирает сам сервер
 * (`input()` в ClickHouse, `jsonb` в PostgreSQL).
 *
 * Почему JSON, а не строка VALUES: значения не экранируются вручную, поэтому
 * инъекции взяться неоткуда, а весь батч уезжает одним запросом — на него
 * можно натянуть произвольный SQL с агрегацией и JOIN.
 *
 * Только заголовок со static-функциями: каждый плагин — отдельная .so.
 */

#ifndef DBSINK_H
#define DBSINK_H

#include <fluent-bit/flb_output_plugin.h>
#include <fluent-bit/flb_pack.h>
#include <fluent-bit/flb_sds.h>
#include <fluent-bit/flb_time.h>
#include <fluent-bit/flb_log_event_decoder.h>

#include <cmetrics/cmetrics.h>
#include <cmetrics/cmt_map.h>
#include <cmetrics/cmt_metric.h>
#include <cmetrics/cmt_counter.h>
#include <cmetrics/cmt_gauge.h>
#include <cmetrics/cmt_untyped.h>
#include <cmetrics/cmt_decode_msgpack.h>

#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

/* JSON пишется здесь, а не штатным flb_msgpack_raw_to_json_sds. Причины две:
 * у той функции между 4.0 и 4.2 сменилась сигнатура (добавился escape_unicode),
 * а связка «перепаковать запись в промежуточный msgpack и отдать ей» на записи
 * из нескольких полей с экранированием выдавала лишние байты в конце значения.
 * Прямая запись из msgpack-объектов и короче, и промежуточного буфера не
 * требует вовсе. */

/* Как записать время события */
#define DBSINK_TIME_DATETIME64  0   /* 2026-08-24 10:00:00.123456 — понимают обе СУБД */
#define DBSINK_TIME_ISO8601     1   /* 2026-08-24T10:00:00.123456Z */
#define DBSINK_TIME_EPOCH       2   /* 1787598755.123456 */

static int dbsink_time_format_parse(const char *value)
{
    if (!value || *value == '\0' || strcasecmp(value, "datetime64") == 0) {
        return DBSINK_TIME_DATETIME64;
    }
    if (strcasecmp(value, "iso8601") == 0) {
        return DBSINK_TIME_ISO8601;
    }
    if (strcasecmp(value, "epoch") == 0) {
        return DBSINK_TIME_EPOCH;
    }
    return -1;
}

static void dbsink_time_str(struct flb_time *tm, int format,
                            char *out, size_t out_size)
{
    struct tm gm;
    time_t sec = (time_t) tm->tm.tv_sec;
    char base[32];

    if (format == DBSINK_TIME_EPOCH) {
        snprintf(out, out_size, "%" PRIu64 ".%06lu",
                 (uint64_t) tm->tm.tv_sec, (unsigned long) (tm->tm.tv_nsec / 1000));
        return;
    }

    gmtime_r(&sec, &gm);
    if (format == DBSINK_TIME_ISO8601) {
        strftime(base, sizeof(base), "%Y-%m-%dT%H:%M:%S", &gm);
        snprintf(out, out_size, "%s.%06luZ", base,
                 (unsigned long) (tm->tm.tv_nsec / 1000));
    }
    else {
        strftime(base, sizeof(base), "%Y-%m-%d %H:%M:%S", &gm);
        snprintf(out, out_size, "%s.%06lu", base,
                 (unsigned long) (tm->tm.tv_nsec / 1000));
    }
}

/* Строковое значение внутрь JSON. Метки cmetrics — это чужой текст (имя базы,
 * тип ожидания), в нём может оказаться кавычка или перевод строки. */
static void dbsink_json_escape(flb_sds_t *out, const char *in, size_t len)
{
    size_t i;
    char esc[8];
    unsigned char c;

    for (i = 0; i < len; i++) {
        c = (unsigned char) in[i];
        switch (c) {
        case '"':  flb_sds_cat_safe(out, "\\\"", 2); break;
        case '\\': flb_sds_cat_safe(out, "\\\\", 2); break;
        case '\n': flb_sds_cat_safe(out, "\\n", 2);  break;
        case '\r': flb_sds_cat_safe(out, "\\r", 2);  break;
        case '\t': flb_sds_cat_safe(out, "\\t", 2);  break;
        default:
            if (c < 0x20) {
                snprintf(esc, sizeof(esc), "\\u%04x", c);
                flb_sds_cat_safe(out, esc, 6);
            }
            else {
                flb_sds_cat_safe(out, (const char *) &c, 1);
            }
            break;
        }
    }
}

/* ------------------------------------------------------------------ записи */

/* Значение msgpack → JSON. Рекурсия нужна: во вложенных объектах и массивах
 * приезжают и json-столбцы из in_postgres, и метки от фильтров. */
static void dbsink_object_json(msgpack_object *o, flb_sds_t *out)
{
    uint32_t i;

    switch (o->type) {
    case MSGPACK_OBJECT_NIL:
        flb_sds_cat_safe(out, "null", 4);
        break;
    case MSGPACK_OBJECT_BOOLEAN:
        if (o->via.boolean) {
            flb_sds_cat_safe(out, "true", 4);
        }
        else {
            flb_sds_cat_safe(out, "false", 5);
        }
        break;
    case MSGPACK_OBJECT_POSITIVE_INTEGER:
        flb_sds_printf(out, "%" PRIu64, o->via.u64);
        break;
    case MSGPACK_OBJECT_NEGATIVE_INTEGER:
        flb_sds_printf(out, "%" PRId64, o->via.i64);
        break;
    case MSGPACK_OBJECT_FLOAT32:
    case MSGPACK_OBJECT_FLOAT64:
        /* %.17g — столько знаков нужно, чтобы double прочитался обратно
         * без потерь; inf и nan в JSON недопустимы, отдаём null */
        if (o->via.f64 != o->via.f64 ||
            o->via.f64 > 1.7976931348623157e308 ||
            o->via.f64 < -1.7976931348623157e308) {
            flb_sds_cat_safe(out, "null", 4);
        }
        else {
            flb_sds_printf(out, "%.17g", o->via.f64);
        }
        break;
    case MSGPACK_OBJECT_STR:
        flb_sds_cat_safe(out, "\"", 1);
        dbsink_json_escape(out, o->via.str.ptr, o->via.str.size);
        flb_sds_cat_safe(out, "\"", 1);
        break;
    case MSGPACK_OBJECT_BIN:
        flb_sds_cat_safe(out, "\"", 1);
        dbsink_json_escape(out, o->via.bin.ptr, o->via.bin.size);
        flb_sds_cat_safe(out, "\"", 1);
        break;
    case MSGPACK_OBJECT_ARRAY:
        flb_sds_cat_safe(out, "[", 1);
        for (i = 0; i < o->via.array.size; i++) {
            if (i > 0) {
                flb_sds_cat_safe(out, ",", 1);
            }
            dbsink_object_json(&o->via.array.ptr[i], out);
        }
        flb_sds_cat_safe(out, "]", 1);
        break;
    case MSGPACK_OBJECT_MAP:
        flb_sds_cat_safe(out, "{", 1);
        for (i = 0; i < o->via.map.size; i++) {
            if (i > 0) {
                flb_sds_cat_safe(out, ",", 1);
            }
            /* ключ в JSON всегда строка, чем бы он ни был в msgpack */
            if (o->via.map.ptr[i].key.type == MSGPACK_OBJECT_STR) {
                flb_sds_cat_safe(out, "\"", 1);
                dbsink_json_escape(out, o->via.map.ptr[i].key.via.str.ptr,
                                   o->via.map.ptr[i].key.via.str.size);
                flb_sds_cat_safe(out, "\"", 1);
            }
            else {
                flb_sds_cat_safe(out, "\"", 1);
                dbsink_object_json(&o->via.map.ptr[i].key, out);
                flb_sds_cat_safe(out, "\"", 1);
            }
            flb_sds_cat_safe(out, ":", 1);
            dbsink_object_json(&o->via.map.ptr[i].val, out);
        }
        flb_sds_cat_safe(out, "}", 1);
        break;
    default:
        flb_sds_cat_safe(out, "null", 4);
        break;
    }
}

/* Запись целиком. Время добавляется отдельным ключом: в msgpack оно лежит вне
 * тела, а SQL-запросу нужно наравне с остальными полями. */
static int dbsink_record_json(msgpack_object *body, struct flb_time *tm,
                              const char *time_key, int time_format,
                              flb_sds_t *out)
{
    char ts[64];
    uint32_t i;

    if (body->type != MSGPACK_OBJECT_MAP) {
        return -1;
    }

    flb_sds_cat_safe(out, "{", 1);

    if (time_key && *time_key != '\0') {
        flb_sds_cat_safe(out, "\"", 1);
        dbsink_json_escape(out, time_key, strlen(time_key));
        flb_sds_cat_safe(out, "\":", 2);
        if (time_format == DBSINK_TIME_EPOCH) {
            flb_sds_printf(out, "%" PRIu64 ".%06lu", (uint64_t) tm->tm.tv_sec,
                           (unsigned long) (tm->tm.tv_nsec / 1000));
        }
        else {
            dbsink_time_str(tm, time_format, ts, sizeof(ts));
            flb_sds_printf(out, "\"%s\"", ts);
        }
        if (body->via.map.size > 0) {
            flb_sds_cat_safe(out, ",", 1);
        }
    }

    for (i = 0; i < body->via.map.size; i++) {
        if (i > 0) {
            flb_sds_cat_safe(out, ",", 1);
        }
        if (body->via.map.ptr[i].key.type == MSGPACK_OBJECT_STR) {
            flb_sds_cat_safe(out, "\"", 1);
            dbsink_json_escape(out, body->via.map.ptr[i].key.via.str.ptr,
                               body->via.map.ptr[i].key.via.str.size);
            flb_sds_cat_safe(out, "\"", 1);
        }
        else {
            flb_sds_cat_safe(out, "\"?\"", 3);
        }
        flb_sds_cat_safe(out, ":", 1);
        dbsink_object_json(&body->via.map.ptr[i].val, out);
    }

    flb_sds_cat_safe(out, "}", 1);
    return 0;
}

/* -------------------------------------------------------------- метрики */

/* Метка по позиции: ключи лежат в map->label_keys, значения — в metric->labels,
 * и сопоставляются по порядку. Значение может отсутствовать — тогда метка
 * пропускается, как это делает штатный энкодер. */
static void dbsink_metric_labels_json(struct cmt_map *map,
                                      struct cmt_metric *metric,
                                      flb_sds_t *out)
{
    struct cfl_list *head;
    struct cmt_map_label *label_k = NULL;
    struct cmt_map_label *label_v;
    int index = 0;
    int written = 0;

    flb_sds_cat_safe(out, "{", 1);

    if (map->label_count > 0) {
        label_k = cfl_list_entry_first(&map->label_keys,
                                       struct cmt_map_label, _head);
    }

    cfl_list_foreach(head, &metric->labels) {
        if (index >= map->label_count || !label_k) {
            break;
        }
        label_v = cfl_list_entry(head, struct cmt_map_label, _head);
        if (label_k->name && label_v->name) {
            if (written > 0) {
                flb_sds_cat_safe(out, ",", 1);
            }
            flb_sds_cat_safe(out, "\"", 1);
            dbsink_json_escape(out, label_k->name, cfl_sds_len(label_k->name));
            flb_sds_cat_safe(out, "\":\"", 3);
            dbsink_json_escape(out, label_v->name, cfl_sds_len(label_v->name));
            flb_sds_cat_safe(out, "\"", 1);
            written++;
        }
        index++;
        label_k = cfl_list_entry_next(&label_k->_head, struct cmt_map_label,
                                      _head, &map->label_keys);
    }

    flb_sds_cat_safe(out, "}", 1);
}

/* Одна метрика → строка JSON фиксированной формы:
 *   {"name":…, "type":…, "labels":{…}, "value":…, "timestamp":…}
 * Форма фиксирована намеренно: SQL-запрос описывает её один раз в input(),
 * и он не обязан знать, какие именно метрики придут. */
static void dbsink_metric_json(struct cmt_map *map, struct cmt_metric *metric,
                               const char *type, const char *time_key,
                               int time_format, flb_sds_t *out)
{
    struct flb_time tm;
    char ts[64];
    uint64_t nsec;
    double value;

    nsec = cmt_metric_get_timestamp(metric);
    tm.tm.tv_sec = (long) (nsec / 1000000000ULL);
    tm.tm.tv_nsec = (long) (nsec % 1000000000ULL);
    value = cmt_metric_get_value(metric);

    flb_sds_cat_safe(out, "{\"name\":\"", 9);
    flb_sds_cat_safe(out, map->opts->fqname, cfl_sds_len(map->opts->fqname));
    flb_sds_cat_safe(out, "\",\"type\":\"", 10);
    flb_sds_cat_safe(out, type, strlen(type));
    flb_sds_cat_safe(out, "\",\"labels\":", 11);
    dbsink_metric_labels_json(map, metric, out);

    flb_sds_printf(out, ",\"value\":%.17g", value);

    if (time_key && *time_key != '\0') {
        if (time_format == DBSINK_TIME_EPOCH) {
            flb_sds_printf(out, ",\"%s\":%" PRIu64 ".%06lu", time_key,
                           (uint64_t) tm.tm.tv_sec,
                           (unsigned long) (tm.tm.tv_nsec / 1000));
        }
        else {
            dbsink_time_str(&tm, time_format, ts, sizeof(ts));
            flb_sds_printf(out, ",\"%s\":\"%s\"", time_key, ts);
        }
    }
    flb_sds_cat_safe(out, "}", 1);
}

static void dbsink_map_json(struct cmt_map *map, const char *type,
                            const char *time_key, int time_format,
                            const char *separator, flb_sds_t *out, int *count)
{
    struct cfl_list *head;
    struct cmt_metric *metric;

    if (map->metric_static_set == 1) {
        if (*count > 0) {
            flb_sds_cat_safe(out, separator, strlen(separator));
        }
        dbsink_metric_json(map, &map->metric, type, time_key, time_format, out);
        (*count)++;
    }

    cfl_list_foreach(head, &map->metrics) {
        metric = cfl_list_entry(head, struct cmt_metric, _head);
        if (*count > 0) {
            flb_sds_cat_safe(out, separator, strlen(separator));
        }
        dbsink_metric_json(map, metric, type, time_key, time_format, out);
        (*count)++;
    }
}

/* Весь контекст cmetrics → строки. Гистограммы и сводки пропускаются: у них
 * не одно значение, а набор корзин, и в плоскую строку они не ложатся. */
static int dbsink_cmt_json(struct cmt *cmt, const char *time_key,
                           int time_format, const char *separator,
                           flb_sds_t *out, int *count)
{
    struct cfl_list *head;
    struct cmt_counter *counter;
    struct cmt_gauge *gauge;
    struct cmt_untyped *untyped;

    cfl_list_foreach(head, &cmt->counters) {
        counter = cfl_list_entry(head, struct cmt_counter, _head);
        dbsink_map_json(counter->map, "counter", time_key, time_format,
                        separator, out, count);
    }
    cfl_list_foreach(head, &cmt->gauges) {
        gauge = cfl_list_entry(head, struct cmt_gauge, _head);
        dbsink_map_json(gauge->map, "gauge", time_key, time_format,
                        separator, out, count);
    }
    cfl_list_foreach(head, &cmt->untypeds) {
        untyped = cfl_list_entry(head, struct cmt_untyped, _head);
        dbsink_map_json(untyped->map, "untyped", time_key, time_format,
                        separator, out, count);
    }
    return 0;
}

/* Пачка событий → JSON. separator: "\n" для JSONEachRow, "," для массива.
 * Возвращает число строк или -1. */
static int dbsink_chunk_json(struct flb_event_chunk *chunk,
                             const char *time_key, int time_format,
                             const char *separator, flb_sds_t *out)
{
    struct flb_log_event_decoder decoder;
    struct flb_log_event event;
    struct cmt *cmt;
    size_t off = 0;
    int count = 0;
    int ret;

    if (chunk->type == FLB_EVENT_TYPE_METRICS) {
        while (cmt_decode_msgpack_create(&cmt, (char *) chunk->data,
                                         chunk->size, &off) ==
               CMT_DECODE_MSGPACK_SUCCESS) {
            dbsink_cmt_json(cmt, time_key, time_format, separator, out, &count);
            cmt_destroy(cmt);
        }
        return count;
    }

    ret = flb_log_event_decoder_init(&decoder, (char *) chunk->data, chunk->size);
    if (ret != FLB_EVENT_DECODER_SUCCESS) {
        return -1;
    }

    while (flb_log_event_decoder_next(&decoder, &event) ==
           FLB_EVENT_DECODER_SUCCESS) {
        if (count > 0) {
            flb_sds_cat_safe(out, separator, strlen(separator));
        }
        if (dbsink_record_json(event.body, &event.timestamp, time_key,
                               time_format, out) != 0) {
            continue;
        }
        count++;
    }

    flb_log_event_decoder_destroy(&decoder);
    return count;
}

#endif
