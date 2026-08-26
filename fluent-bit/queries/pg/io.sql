-- pg_stat_io появилась в 16, на серверах младше запрос вернёт ошибку —
-- заводите отдельный [INPUT] со своим списком Targets.
-- Нулевые строки отбрасываем: их там большинство и они ничего не показывают.
-- метки: backend_type, object, context
-- значения: reads, writes, writebacks, extends, hits, evictions, fsyncs,
--           read_time, write_time
SELECT
    backend_type, object, context,
    reads, writes, writebacks, extends, hits, evictions, fsyncs,
    read_time, write_time,
    stats_reset::text AS stats_reset,
    now()::text       AS snap_ts
FROM pg_stat_io
WHERE coalesce(reads,0) + coalesce(writes,0) + coalesce(hits,0)
    + coalesce(extends,0) + coalesce(evictions,0) > 0
