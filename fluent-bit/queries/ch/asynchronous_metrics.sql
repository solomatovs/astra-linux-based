-- Медленные показатели ноды: место на дисках, память, аптайм. Обновляются
-- раз в asynchronous_metrics_update_period_s (по умолчанию 1 с), но меняются
-- медленно — опрашивать раз в 15..60 с достаточно.
--
-- Строки этой таблицы — пары имя/значение, поэтому metric идёт меткой, а не
-- превращается в отдельные метрики: имена там зависят от числа дисков.
-- метки: metric   значения: value
SELECT
    hostName() AS instance,
    metric,
    value
FROM system.asynchronous_metrics
WHERE metric IN (
    'Uptime', 'OSMemoryTotal', 'OSMemoryAvailable', 'MemoryResident',
    'LoadAverage1', 'OSUptime',
    'jemalloc.resident', 'MarkCacheBytes', 'UncompressedCacheBytes',
    'NumberOfTables', 'NumberOfDatabases', 'MaxPartCountForPartition',
    'ReplicasMaxAbsoluteDelay', 'ReplicasMaxQueueSize',
    'TotalBytesOfMergeTreeTables', 'TotalRowsOfMergeTreeTables',
    'TotalPartsOfMergeTreeTables'
)
   OR metric LIKE 'DiskAvailable%'
   OR metric LIKE 'DiskTotal%'
   OR metric LIKE 'DiskUsed%'
FORMAT JSONEachRow
