-- Показатели ноды из system.metric_log: в ProfileEvent_* лежат ДЕЛЬТЫ за
-- интервал сбора (по умолчанию секунда), то есть готовые скорости — считать
-- разницу снимков не нужно. CurrentMetric_* — мгновенные значения.
--
-- Требует, чтобы metric_log был включён (по умолчанию включён).
-- метки: —   значения: все числовые
SELECT
    hostName()  AS instance,
    toString(event_time) AS event_ts,
    ProfileEvent_InsertedRows       AS inserted_rows,
    ProfileEvent_InsertedBytes      AS inserted_bytes,
    ProfileEvent_SelectedRows       AS selected_rows,
    ProfileEvent_SelectedBytes      AS selected_bytes,
    ProfileEvent_MergedRows         AS merged_rows,
    ProfileEvent_InsertQuery        AS insert_queries,
    ProfileEvent_SelectQuery        AS select_queries,
    ProfileEvent_FailedQuery        AS failed_queries,
    ProfileEvent_DelayedInserts     AS delayed_inserts,
    ProfileEvent_RejectedInserts    AS rejected_inserts,
    ProfileEvent_MarkCacheHits      AS mark_cache_hits,
    ProfileEvent_MarkCacheMisses    AS mark_cache_misses,
    CurrentMetric_Merge             AS merges_running,
    CurrentMetric_Query             AS queries_running,
    CurrentMetric_TCPConnection     AS tcp_connections,
    CurrentMetric_HTTPConnection    AS http_connections,
    CurrentMetric_ReadonlyReplica   AS readonly_replicas,
    CurrentMetric_MemoryTracking    AS memory_bytes
FROM system.metric_log
ORDER BY event_time DESC
LIMIT 1
FORMAT JSONEachRow
