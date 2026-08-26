-- pg_stat_checkpointer появилась в 17: до неё те же счётчики жили в
-- pg_stat_bgwriter. Для 14..16 — checkpointer-16.sql, имена столбцов там
-- приведены к этому же виду.
-- метки: —   значения: num_timed, num_requested, buffers_written,
--                      write_time, sync_time
SELECT
    num_timed, num_requested,
    buffers_written,
    write_time, sync_time,
    stats_reset::text AS stats_reset,
    now()::text       AS snap_ts
FROM pg_stat_checkpointer
