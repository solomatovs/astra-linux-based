-- Столбцы контрольных точек живут в pg_stat_bgwriter до 16 и в
-- pg_stat_checkpointer начиная с 17, поэтому здесь только то, что есть везде.
-- Контрольные точки — отдельным запросом checkpointer.sql.
-- метки: —   значения: buffers_clean, maxwritten_clean, buffers_alloc
SELECT
    buffers_clean,
    maxwritten_clean,
    buffers_alloc,
    stats_reset::text AS stats_reset,
    now()::text       AS snap_ts
FROM pg_stat_bgwriter
