-- pg_stat_statements: счётчики по запросам. Расширение нужно в
-- shared_preload_libraries, то есть требует рестарта сервера, — поэтому у
-- запроса обычно свой список Targets.
--
-- В режиме metrics меткой берите queryid (он стабилен), но не query: текст
-- запроса в метке раздувает ряд и мешает его сравнивать между версиями.
-- метки: queryid   значения: calls, rows, total_exec_time, mean_exec_time, …
SELECT
    queryid::text AS queryid,
    left(query, 300) AS query,
    calls, rows,
    total_exec_time, mean_exec_time, max_exec_time,
    shared_blks_hit, shared_blks_read, shared_blks_dirtied, shared_blks_written,
    temp_blks_read, temp_blks_written,
    wal_records, wal_fpi, wal_bytes,
    now()::text AS snap_ts
FROM pg_stat_statements
ORDER BY total_exec_time DESC
LIMIT 50
