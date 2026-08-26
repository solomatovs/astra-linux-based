-- Очередь блокировок: только неудовлетворённые, а не полный список.
-- Свёрнуто до счётчика — pid метрике не нужен, для разбора конкретного затора
-- есть тот же запрос без GROUP BY в режиме logs.
-- метки: locktype, mode, relation   значения: waiting, max_wait_sec
SELECT
    l.locktype,
    l.mode,
    coalesce(c.relname, '')  AS relation,
    count(*)::bigint         AS waiting,
    coalesce(max(extract(epoch FROM (now() - a.xact_start))), 0) AS max_wait_sec,
    now()::text              AS snap_ts
FROM pg_locks l
LEFT JOIN pg_class c ON c.oid = l.relation
LEFT JOIN pg_stat_activity a ON a.pid = l.pid
WHERE NOT l.granted
GROUP BY 1, 2, 3
