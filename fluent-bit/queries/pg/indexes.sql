-- Индексы: кем пользуются, а кем нет. idx_scan = 0 при заметном размере —
-- кандидат на удаление.
-- метки: schema, table, index   значения: idx_scan, idx_tup_read,
--                                         idx_tup_fetch, size_bytes
SELECT
    schemaname   AS schema,
    relname      AS table,
    indexrelname AS index,
    idx_scan, idx_tup_read, idx_tup_fetch,
    pg_relation_size(indexrelid) AS size_bytes,
    now()::text  AS snap_ts
FROM pg_stat_user_indexes
ORDER BY pg_relation_size(indexrelid) DESC
LIMIT 64
