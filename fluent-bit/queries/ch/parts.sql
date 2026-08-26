-- Куски активных партиций, свёрнутые до таблицы: в метрику каждый кусок по
-- отдельности не годится — их тысячи, и имена у них живут по несколько минут.
-- Полный список кусков (для режима logs) — в parts-detail.sql.
-- метки: database, table   значения: parts, rows, bytes_on_disk,
--                                    uncompressed_bytes, max_level
SELECT
    hostName() AS instance,
    database,
    table,
    count()                     AS parts,
    sum(rows)                   AS rows,
    sum(bytes_on_disk)          AS bytes_on_disk,
    sum(data_uncompressed_bytes) AS uncompressed_bytes,
    max(level)                  AS max_level,
    uniqExact(partition)        AS partitions
FROM system.parts
WHERE active AND database NOT IN ('system', 'INFORMATION_SCHEMA', 'information_schema')
GROUP BY database, table
ORDER BY bytes_on_disk DESC
LIMIT 200
FORMAT JSONEachRow
