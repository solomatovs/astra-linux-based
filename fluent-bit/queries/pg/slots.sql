-- Слоты репликации: сколько WAL они удерживают. Забытый слот — это то, из-за
-- чего кончается место под pg_wal, поэтому retained_bytes здесь главное.
-- метки: slot_name, slot_type, database, wal_status
-- значения: retained_bytes, safe_wal_size, is_active
SELECT
    slot_name, slot_type, coalesce(database, '') AS database,
    active::int    AS is_active,
    temporary::int AS is_temporary,
    coalesce(wal_status, '') AS wal_status,
    pg_wal_lsn_diff(
        CASE WHEN pg_is_in_recovery() THEN pg_last_wal_replay_lsn()
             ELSE pg_current_wal_lsn() END, restart_lsn)::bigint AS retained_bytes,
    coalesce(safe_wal_size, 0) AS safe_wal_size,
    now()::text AS snap_ts
FROM pg_replication_slots
