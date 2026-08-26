-- Ручки сервера. Не все 358, а те, от которых зависит поведение под нагрузкой.
-- Запрос для режима logs: setting — строка ('4GB', 'on'), в метрику она не
-- превращается. Опрашивать редко (раз в минуту и реже).
SELECT name, setting, unit, source, boot_val
FROM pg_settings
WHERE name IN (
    'shared_buffers', 'work_mem', 'maintenance_work_mem', 'effective_cache_size',
    'max_connections', 'max_wal_size', 'min_wal_size', 'wal_buffers',
    'checkpoint_timeout', 'checkpoint_completion_target', 'synchronous_commit',
    'wal_level', 'autovacuum', 'autovacuum_max_workers', 'autovacuum_naptime',
    'autovacuum_vacuum_scale_factor', 'autovacuum_vacuum_cost_limit',
    'bgwriter_delay', 'bgwriter_lru_maxpages', 'random_page_cost',
    'default_statistics_target', 'max_worker_processes', 'max_parallel_workers'
)
