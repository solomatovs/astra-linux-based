-- Содержимое разделяемых буферов: чем именно они заняты и что в них грязного.
-- Нужно расширение pg_buffercache. Один проход со свёрткой на сервере — тащить
-- по строке на буфер незачем. Строка «(свободно)» — незанятые буферы, она и
-- показывает, насколько shared_buffers вообще используется.
-- метки: schema, relation   значения: buffers, dirty, hot, avg_usagecount
SELECT
    coalesce(n.nspname, '')          AS schema,
    coalesce(c.relname,
        CASE WHEN b.relfilenode IS NULL THEN '(свободно)'
             ELSE '(другая база)' END) AS relation,
    count(*)::bigint                              AS buffers,
    count(*) FILTER (WHERE b.isdirty)::bigint     AS dirty,
    count(*) FILTER (WHERE b.usagecount >= 3)::bigint AS hot,
    round(avg(b.usagecount)::numeric, 2)          AS avg_usagecount,
    now()::text                                   AS snap_ts
FROM pg_buffercache b
LEFT JOIN pg_class c
       ON c.relfilenode = b.relfilenode
      AND b.reldatabase IN (0, (SELECT oid FROM pg_database WHERE datname = current_database()))
LEFT JOIN pg_namespace n ON n.oid = c.relnamespace
GROUP BY 1, 2
ORDER BY 3 DESC
LIMIT 64
