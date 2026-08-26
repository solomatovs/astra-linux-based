-- Реплики глазами ведущего. На сервере без реплик строк не будет — это
-- нормальный ответ, а не сбой.
-- метки: application_name, client_addr, state, sync_state
-- значения: write_lag_sec, flush_lag_sec, replay_lag_sec, replay_bytes
SELECT
    application_name,
    coalesce(client_addr::text, 'local') AS client_addr,
    state,
    sync_state,
    coalesce(sent_lsn::text, '')   AS sent_lsn,
    coalesce(replay_lsn::text, '') AS replay_lsn,
    coalesce(pg_wal_lsn_diff(sent_lsn, replay_lsn)::bigint, 0) AS replay_bytes,
    extract(epoch FROM write_lag)  AS write_lag_sec,
    extract(epoch FROM flush_lag)  AS flush_lag_sec,
    extract(epoch FROM replay_lag) AS replay_lag_sec,
    now()::text                    AS snap_ts
FROM pg_stat_replication
