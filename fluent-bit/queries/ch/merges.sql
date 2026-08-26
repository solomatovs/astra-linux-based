-- Мержи, идущие прямо сейчас. Свёрнуто по таблице: имя результирующего куска
-- в метке жило бы ровно до конца мержа. Полный список — в режиме logs тем же
-- запросом без GROUP BY.
-- метки: database, table   значения: merges, mutations, max_elapsed_sec,
--                                    min_progress, rows_read, rows_written
SELECT
    hostName() AS instance,
    database,
    table,
    count()                          AS merges,
    countIf(is_mutation)             AS mutations,
    max(elapsed)                     AS max_elapsed_sec,
    min(progress)                    AS min_progress,
    sum(num_parts)                   AS source_parts,
    sum(total_size_bytes_compressed) AS size_bytes,
    sum(rows_read)                   AS rows_read,
    sum(rows_written)                AS rows_written
FROM system.merges
GROUP BY database, table
LIMIT 200
FORMAT JSONEachRow
