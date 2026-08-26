-- Проба ожиданий: срез того, чего ждут бэкенды, свёрнутый в гистограмму.
-- Профиль получается накоплением проб на стороне получателя — одна проба сама
-- по себе ничего не значит, важно распределение. Опрашивать часто (1 с).
-- метки: wait_event_type, wait_event, state, backend_type
-- значения: backends
SELECT
    coalesce(wait_event_type, 'Running') AS wait_event_type,
    coalesce(wait_event, '-')            AS wait_event,
    coalesce(state, 'unknown')           AS state,
    backend_type,
    count(*)::bigint                     AS backends,
    now()::text                          AS sample_ts
FROM pg_stat_activity
WHERE backend_type IS NOT NULL
GROUP BY 1, 2, 3, 4
