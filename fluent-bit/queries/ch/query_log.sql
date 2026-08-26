-- Завершённые запросы: тот же приём с курсором, что и в part_log.sql.
-- Берём только финальные события (type > 1), иначе каждый запрос приедет
-- дважды — на старте и на завершении.
--
-- Конфигурация: Cursor_Field event_ts, Time_Field event_ts, Cursor_File …
SELECT
    hostName() AS instance,
    toString(event_time_microseconds) AS event_ts,
    toString(type)     AS type,
    query_id,
    user,
    toString(query_kind) AS query_kind,
    left(query, 300)   AS query,
    query_duration_ms,
    read_rows, read_bytes,
    written_rows, written_bytes,
    result_rows, result_bytes,
    memory_usage,
    exception_code,
    left(exception, 200) AS exception
FROM system.query_log
WHERE event_time_microseconds > {CURSOR}
  AND type > 1
ORDER BY event_time_microseconds
LIMIT 2000
FORMAT JSONEachRow
