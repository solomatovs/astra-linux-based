-- Только столбцы, общие для 14..18: в 18 wal_write/wal_sync и их времена убрали
-- из pg_stat_wal, они переехали в pg_stat_io (object='wal').
-- метки: —   значения: wal_records, wal_fpi, wal_bytes, wal_buffers_full
SELECT
    wal_records, wal_fpi, wal_bytes, wal_buffers_full,
    stats_reset::text AS stats_reset,
    now()::text       AS snap_ts,
    CASE WHEN pg_is_in_recovery()
         THEN pg_last_wal_replay_lsn()::text
         ELSE pg_current_wal_lsn()::text END AS lsn
FROM pg_stat_wal
