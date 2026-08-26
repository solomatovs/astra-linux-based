-- То же для 14..16, где счётчики контрольных точек ещё в pg_stat_bgwriter.
-- Имена столбцов приведены к виду pg_stat_checkpointer, чтобы у получателя
-- метрика называлась одинаково независимо от версии сервера.
-- метки: —   значения: num_timed, num_requested, buffers_written,
--                      write_time, sync_time
SELECT
    checkpoints_timed         AS num_timed,
    checkpoints_req           AS num_requested,
    buffers_checkpoint        AS buffers_written,
    checkpoint_write_time     AS write_time,
    checkpoint_sync_time      AS sync_time,
    stats_reset::text         AS stats_reset,
    now()::text               AS snap_ts
FROM pg_stat_bgwriter
