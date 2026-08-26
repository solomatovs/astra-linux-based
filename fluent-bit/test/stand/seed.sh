#!/bin/bash
# Наполнение стенда данными. На пустом сервере половина системных таблиц пуста
# или ещё не создана: system.part_log появляется только после первого куска,
# pg_stat_user_tables — только когда в базе есть таблицы.
#
# Запускается с хоста, стенд должен быть уже поднят.
set -eu

cd "$(dirname "$0")"
DC=(docker compose -f docker-compose.yml)

ch() { "${DC[@]}" exec -T clickhouse clickhouse client --query "$1"; }
pg() { "${DC[@]}" exec -T -e PGPASSWORD=fluentbit postgres \
           psql -U postgres -d postgres -Atc "$1"; }

echo "== clickhouse =="
ch "CREATE DATABASE IF NOT EXISTS demo"
ch "CREATE TABLE IF NOT EXISTS demo.events (
        ts DateTime,
        id UInt64,
        kind LowCardinality(String),
        payload String
    ) ENGINE = MergeTree
    PARTITION BY toYYYYMMDD(ts)
    ORDER BY (kind, ts, id)"
# несколько вставок подряд — несколько кусков, отсюда мержи и part_log
for _ in 1 2 3 4; do
    ch "INSERT INTO demo.events
        SELECT now() - number % 86400, number,
               ['insert','select','merge'][number % 3 + 1], repeat('x', 64)
        FROM numbers(200000)"
done
ch "OPTIMIZE TABLE demo.events"
echo "активных кусков: $(ch "SELECT count() FROM system.parts WHERE database='demo' AND active")"

# цели выходных плагинов: агрегат «логов nginx» и метрики самого fluent-bit
ch "DROP TABLE IF EXISTS demo.nginx_agg"
ch "CREATE TABLE demo.nginx_agg (
        minute DateTime,
        host String,
        status UInt16,
        hits UInt64,
        bytes UInt64
    ) ENGINE = SummingMergeTree ORDER BY (minute, host, status)"
pg "DROP TABLE IF EXISTS flb_metrics"
pg "CREATE TABLE flb_metrics (
        name text,
        type text,
        labels jsonb,
        value double precision,
        timestamp timestamptz)"

# целевая таблица насоса pg2ch: пересоздаётся, чтобы прогон был воспроизводим
ch "DROP TABLE IF EXISTS demo.pg_events"
ch "CREATE TABLE demo.pg_events (
        id Int64,
        ts DateTime64(6, 'UTC'),
        kind String,
        payload String
    ) ENGINE = MergeTree ORDER BY id"

echo "== postgres =="
pg "CREATE TABLE IF NOT EXISTS demo_events (
        id bigserial PRIMARY KEY,
        ts timestamptz DEFAULT now(),
        kind text,
        payload text)"
pg "INSERT INTO demo_events (kind, payload)
    SELECT (ARRAY['insert','select','merge'])[1 + i % 3], repeat('x', 64)
    FROM generate_series(1, 50000) i"
pg "CREATE INDEX IF NOT EXISTS demo_events_kind_idx ON demo_events (kind)"
pg "UPDATE demo_events SET payload = payload || 'y' WHERE id % 7 = 0"
pg "ANALYZE demo_events"
echo "строк: $(pg "SELECT count(*) FROM demo_events")"

echo "== готово, метрики появятся в течение одного интервала опроса =="
