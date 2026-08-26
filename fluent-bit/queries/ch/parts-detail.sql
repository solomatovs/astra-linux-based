-- Полный список активных кусков — для режима logs. В метрики его пускать
-- нельзя: имя куска в метке порождает новый временной ряд на каждый мерж.
SELECT
    hostName()          AS instance,
    toString(now64(3))  AS snap_ts,
    database,
    table,
    partition,
    name,
    part_type,
    level,
    rows,
    bytes_on_disk,
    marks,
    toString(modification_time) AS modification_time
FROM system.parts
WHERE active AND database NOT IN ('system', 'INFORMATION_SCHEMA', 'information_schema')
ORDER BY database, table, partition, name
LIMIT 5000
FORMAT JSONEachRow
