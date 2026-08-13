CREATE DATABASE IF NOT EXISTS smoke;

CREATE TABLE IF NOT EXISTS smoke.events
(
    id      UInt64,
    day     Date,
    name    String,
    value   Float64
)
ENGINE = MergeTree
PARTITION BY toYYYYMM(day)
ORDER BY id;

INSERT INTO smoke.events
SELECT
    number                                  AS id,
    toDate('2024-01-01') + (number % 90)    AS day,
    concat('event-', toString(number % 17)) AS name,
    number * 1.5                            AS value
FROM numbers(1000);
