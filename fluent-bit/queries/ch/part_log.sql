-- Журнал кусков: поток событий, поэтому с курсором. {CURSOR} подставляет
-- плагин — последнее увиденное event_ts (или Cursor_Default на первом запросе).
--
-- Столбец event_time_microseconds в SELECT переименован в event_ts намеренно:
-- под своим именем строковый алиас подменяет столбец в WHERE, и сравнение
-- DateTime64 со String падает.
--
-- Конфигурация: Cursor_Field event_ts, Time_Field event_ts, Cursor_File …
SELECT
    hostName()  AS instance,
    toString(event_time_microseconds) AS event_ts,
    toString(event_type)    AS event_type,
    toString(merge_reason)  AS merge_reason,
    database,
    table,
    part_name,
    partition,
    part_type,
    rows,
    size_in_bytes,
    duration_ms,
    merged_from,
    error
FROM system.part_log
WHERE event_time_microseconds > {CURSOR}
  AND database NOT IN ('system', 'INFORMATION_SCHEMA', 'information_schema')
ORDER BY event_time_microseconds
LIMIT 2000
FORMAT JSONEachRow
