-- Архиватор WAL: последний успех и накопившиеся отказы. Растущий failed_count
-- при неизменном archived_count — сегменты не уезжают, место кончится.
-- метки: —   значения: archived_count, failed_count
SELECT
    archived_count, failed_count,
    coalesce(last_archived_wal, '')         AS last_archived_wal,
    coalesce(last_archived_time::text, '')  AS last_archived_time,
    coalesce(last_failed_wal, '')           AS last_failed_wal,
    coalesce(last_failed_time::text, '')    AS last_failed_time,
    extract(epoch FROM (now() - last_archived_time))::bigint AS last_archived_age_sec,
    stats_reset::text AS stats_reset,
    now()::text       AS snap_ts
FROM pg_stat_archiver
