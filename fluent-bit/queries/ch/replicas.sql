-- Реплицируемые таблицы: очередь и отставание. is_readonly = 1 означает, что
-- нода потеряла ZooKeeper/Keeper и таблица дальше не принимает запись.
-- метки: database, table   значения: is_readonly, is_session_expired,
--                                    future_parts, parts_to_check, queue_size,
--                                    inserts_in_queue, merges_in_queue,
--                                    absolute_delay, log_delay
SELECT
    hostName() AS instance,
    database,
    table,
    is_readonly,
    is_session_expired,
    future_parts,
    parts_to_check,
    queue_size,
    inserts_in_queue,
    merges_in_queue,
    absolute_delay,
    (log_max_index - log_pointer) AS log_delay,
    total_replicas,
    active_replicas
FROM system.replicas
LIMIT 500
FORMAT JSONEachRow
