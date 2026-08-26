-- Колонки: тип, кодек и то, насколько колонка реально сжалась.
--
-- Размеры берутся из system.parts_columns, а не из system.columns: у
-- Compact-кусков колонки лежат одним файлом, и в system.columns размеры
-- остаются нулями.
-- метки: database, table, name   значения: data_compressed_bytes,
--                                          data_uncompressed_bytes, rows
SELECT
    hostName()  AS instance,
    c.database  AS database,
    c.table     AS table,
    c.name      AS name,
    c.type      AS type,
    c.position  AS position,
    c.compression_codec AS compression_codec,
    c.is_in_sorting_key AS is_in_sorting_key,
    c.is_in_partition_key AS is_in_partition_key,
    pc.ubytes   AS data_uncompressed_bytes,
    pc.cbytes   AS data_compressed_bytes,
    pc.nrows    AS rows
FROM system.columns AS c
LEFT JOIN
(
    SELECT
        database,
        table,
        column,
        sum(column_data_uncompressed_bytes) AS ubytes,
        sum(column_data_compressed_bytes)   AS cbytes,
        sum(rows) AS nrows
    FROM system.parts_columns
    WHERE active
    GROUP BY database, table, column
) AS pc ON pc.database = c.database AND pc.table = c.table AND pc.column = c.name
WHERE c.database NOT IN ('system', 'INFORMATION_SCHEMA', 'information_schema')
ORDER BY c.database, c.table, c.position
LIMIT 5000
FORMAT JSONEachRow
