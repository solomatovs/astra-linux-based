-- Карта видимости: сколько страниц таблицы вакуум уже пометил как целиком
-- видимые и целиком замороженные. Это долг перед заморозкой — то, из-за чего
-- однажды приходит агрессивный autovacuum. Нужно расширение pg_visibility.
-- метки: schema, table   значения: pages, all_visible, all_frozen, frozen_age
SELECT
    n.nspname             AS schema,
    c.relname             AS table,
    c.relpages::bigint    AS pages,
    v.all_visible::bigint AS all_visible,
    v.all_frozen::bigint  AS all_frozen,
    age(c.relfrozenxid)::bigint AS frozen_age,
    now()::text           AS snap_ts
FROM pg_class c
JOIN pg_namespace n ON n.oid = c.relnamespace
CROSS JOIN LATERAL pg_visibility_map_summary(c.oid) v
WHERE c.relkind = 'r'
  AND n.nspname NOT IN ('pg_catalog', 'information_schema')
  AND c.relpages > 0
ORDER BY c.relpages DESC
LIMIT 32
