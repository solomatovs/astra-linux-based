-- Оценка раздувания по системной статистике, без pgstattuple: тот считает
-- точно, но полным проходом по таблице — это нагрузка на боевой сервер.
-- Здесь отношение занятых страниц к тому, сколько заняли бы строки при их
-- средней ширине. Это оценка, и она так и подписана: est_bloat_factor.
-- метки: schema, table   значения: est_tuples, pages, heap_bytes,
--                                  avg_row_width, est_bloat_factor
SELECT
    n.nspname           AS schema,
    c.relname           AS table,
    c.reltuples::bigint AS est_tuples,
    c.relpages::bigint  AS pages,
    pg_relation_size(c.oid) AS heap_bytes,
    s.avg_width_sum     AS avg_row_width,
    CASE WHEN c.relpages > 0 AND c.reltuples > 0 AND coalesce(s.avg_width_sum, 0) > 0
         THEN round(((c.relpages * 8192.0) /
                     (c.reltuples::numeric * (s.avg_width_sum + 24)))::numeric, 2)
         ELSE NULL END  AS est_bloat_factor,
    now()::text         AS snap_ts
FROM pg_class c
JOIN pg_namespace n ON n.oid = c.relnamespace
LEFT JOIN LATERAL (
    SELECT sum(a.avg_width)::int AS avg_width_sum
    FROM pg_stats a WHERE a.schemaname = n.nspname AND a.tablename = c.relname
) s ON TRUE
WHERE c.relkind = 'r' AND n.nspname NOT IN ('pg_catalog', 'information_schema')
ORDER BY pg_relation_size(c.oid) DESC
LIMIT 64
