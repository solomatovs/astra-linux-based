-- Таблицы текущей базы: размер, мусор и работа вакуума. Статистика в
-- pg_stat_user_tables — только для той базы, к которой подключён сбор.
-- LIMIT здесь не украшение: с меткой на таблицу каждая строка — свой ряд.
-- метки: schema, table   значения: все числовые
SELECT
    schemaname            AS schema,
    relname               AS table,
    n_live_tup, n_dead_tup,
    n_tup_ins, n_tup_upd, n_tup_del, n_tup_hot_upd,
    seq_scan, idx_scan,
    heap_blks_read, heap_blks_hit, idx_blks_read, idx_blks_hit,
    vacuum_count, autovacuum_count, analyze_count, autoanalyze_count,
    last_autovacuum::text AS last_autovacuum,
    pg_total_relation_size(relid)                        AS total_bytes,
    pg_relation_size(relid)                              AS heap_bytes,
    pg_indexes_size(relid)                               AS index_bytes,
    now()::text           AS snap_ts
FROM pg_stat_user_tables
JOIN pg_statio_user_tables USING (relid, schemaname, relname)
ORDER BY pg_total_relation_size(relid) DESC
LIMIT 64
