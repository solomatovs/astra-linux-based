-- Бэкенды: кто есть и чего ждёт. Текст чужих запросов виден только роли с
-- pg_read_all_stats, поэтому его может не быть — это не ошибка сбора.
--
-- Это запрос для режима logs: pid и текст запроса метками делать нельзя,
-- каждый новый pid породил бы свой временной ряд. Числа по бэкендам берите
-- из waits.sql, там они уже свёрнуты.
SELECT
    pid,
    datname            AS database,
    usename            AS username,
    application_name,
    coalesce(state, 'unknown')  AS state,
    wait_event_type,
    wait_event,
    backend_type,
    left(coalesce(query, ''), 200) AS query,
    extract(epoch FROM (now() - xact_start))   AS xact_age_sec,
    extract(epoch FROM (now() - query_start))  AS query_age_sec,
    extract(epoch FROM (now() - state_change)) AS state_age_sec,
    now()::text        AS snap_ts
FROM pg_stat_activity
WHERE backend_type IS NOT NULL
