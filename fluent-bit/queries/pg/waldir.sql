-- Настоящие сегменты pg_wal: не размер каталога, а их список. Требует роли
-- pg_monitor.
-- метки: —   значения: segments, bytes
SELECT
    count(*)::bigint        AS segments,
    sum(size)::bigint       AS bytes,
    min(name)               AS oldest,
    max(name)               AS newest,
    max(modification)::text AS newest_modified,
    now()::text             AS snap_ts
FROM pg_ls_waldir()
