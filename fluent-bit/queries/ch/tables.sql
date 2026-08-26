-- Метаданные и размеры таблиц MergeTree: движок, ключи, строки, байты.
-- Меняются редко — опрашивать раз в 30..60 с.
-- метки: database, table, engine   значения: total_rows, total_bytes,
--                                            partitions, parts
SELECT
    hostName() AS instance,
    database,
    name       AS table,
    engine,
    partition_key,
    sorting_key,
    total_rows,
    total_bytes,
    (SELECT uniqExact(partition) FROM system.parts p
      WHERE p.active AND p.database = t.database AND p.table = t.name) AS partitions,
    (SELECT count() FROM system.parts p
      WHERE p.active AND p.database = t.database AND p.table = t.name) AS parts
FROM system.tables t
WHERE engine LIKE '%MergeTree%'
  AND database NOT IN ('system', 'INFORMATION_SCHEMA', 'information_schema')
ORDER BY total_bytes DESC
LIMIT 100
FORMAT JSONEachRow
