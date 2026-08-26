-- Форма кластера: шарды, реплики, порядок узлов. Меняется только при смене
-- конфигурации — опрашивать редко. Запрос для режима logs.
SELECT
    hostName()  AS instance,
    toString(now64(3)) AS snap_ts,
    cluster,
    shard_num,
    replica_num,
    host_name,
    port,
    is_local
FROM system.clusters
ORDER BY cluster, shard_num, replica_num
LIMIT 500
FORMAT JSONEachRow
