-- Кто отвечает и с каких пор. По этому запросу строится список серверов.
-- метки: —   значения: server_version_num, database_bytes, databases, backends,
--                      max_connections
SELECT
    current_setting('server_version')            AS server_version,
    current_setting('server_version_num')::int   AS server_version_num,
    current_database()                           AS database,
    pg_postmaster_start_time()::text             AS started_at,
    extract(epoch FROM (now() - pg_postmaster_start_time()))::bigint AS uptime_sec,
    pg_is_in_recovery()                          AS in_recovery,
    pg_database_size(current_database())         AS database_bytes,
    (SELECT count(*) FROM pg_database WHERE NOT datistemplate) AS databases,
    (SELECT count(*) FROM pg_stat_activity)      AS backends,
    current_setting('max_connections')::int      AS max_connections
