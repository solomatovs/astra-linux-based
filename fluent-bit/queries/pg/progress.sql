-- Ход длительных операций: pg_stat_progress_* показывают то, что идёт прямо
-- сейчас, и исчезают по завершении. Опрашивать надо часто, иначе операция
-- пройдёт между двумя опросами целиком.
--
-- Столбцы у каждой операции свои, поэтому сведены к общему виду. Единицы тоже
-- разные — отсюда столбец unit; складывать done разных kind нельзя.
-- total = 0 означает «объём неизвестен» (COPY из запроса, CLUSTER), а не ноль.
-- метки: kind, relation, phase, unit   значения: done, total, passes
SELECT 'vacuum' AS kind, pid,
       CASE WHEN relid = 0 THEN '' ELSE relid::regclass::text END AS relation,
       phase, 'blocks' AS unit,
       heap_blks_scanned::bigint AS done, heap_blks_total::bigint AS total,
       index_vacuum_count::bigint AS passes, now()::text AS snap_ts
FROM pg_stat_progress_vacuum
UNION ALL
SELECT 'analyze', pid,
       CASE WHEN relid = 0 THEN '' ELSE relid::regclass::text END,
       phase, 'blocks',
       sample_blks_scanned, sample_blks_total, 0, now()::text
FROM pg_stat_progress_analyze
UNION ALL
SELECT 'create_index', pid,
       CASE WHEN relid = 0 THEN '' ELSE relid::regclass::text END,
       phase, 'blocks',
       blocks_done, blocks_total, 0, now()::text
FROM pg_stat_progress_create_index
UNION ALL
SELECT 'cluster', pid,
       CASE WHEN relid = 0 THEN '' ELSE relid::regclass::text END,
       phase, 'tuples',
       heap_tuples_scanned, 0, heap_tuples_written, now()::text
FROM pg_stat_progress_cluster
UNION ALL
SELECT 'copy', pid,
       CASE WHEN relid = 0 THEN '' ELSE relid::regclass::text END,
       command || ' ' || type, 'tuples',
       tuples_processed, 0, bytes_processed, now()::text
FROM pg_stat_progress_copy
