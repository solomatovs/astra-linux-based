-- pg_stat_database — счётчики с момента сброса, а не скорости: превращать их
-- в скорости должен получатель. У prometheus для этого есть rate(), поэтому
-- отдавать надо именно счётчик, а не посчитанную на клиенте разность.
-- метки: database   значения: все числовые
SELECT
    datname                     AS database,
    numbackends,
    xact_commit, xact_rollback,
    blks_read, blks_hit,
    tup_returned, tup_fetched, tup_inserted, tup_updated, tup_deleted,
    conflicts, deadlocks,
    temp_files, temp_bytes,
    blk_read_time, blk_write_time,
    pg_database_size(datname)   AS size_bytes,
    stats_reset::text           AS stats_reset,
    now()::text                 AS snap_ts
FROM pg_stat_database
WHERE datname IS NOT NULL AND datname NOT LIKE 'template%'
