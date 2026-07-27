-- Проверка опций, ради которых собирается своя libsqlite3.
-- .bail on валит скрипт на первой же ошибке, поэтому отсутствие модуля
-- (FTS5, RTREE, ...) даёт ненулевой код возврата.

.bail on
.mode list

-- FTS5 с trigram-токенайзером: на нём держится поиск по истории hermes
CREATE VIRTUAL TABLE docs USING fts5(content, tokenize='trigram');
INSERT INTO docs VALUES ('hermes agent'), ('boba chainlit');
SELECT 'fts5 trigram: ' || CASE
    WHEN (SELECT count(*) FROM docs WHERE docs MATCH 'erm') = 1 THEN 'ok'
    ELSE 'FAIL' END;

CREATE VIRTUAL TABLE legacy USING fts4(content);
INSERT INTO legacy VALUES ('legacy full text');
SELECT 'fts4: ' || CASE
    WHEN (SELECT count(*) FROM legacy WHERE legacy MATCH 'full') = 1 THEN 'ok'
    ELSE 'FAIL' END;

CREATE VIRTUAL TABLE boxes USING rtree(id, minx, maxx, miny, maxy);
INSERT INTO boxes VALUES (1, 0.0, 1.0, 0.0, 1.0);
SELECT 'rtree: ' || CASE
    WHEN (SELECT count(*) FROM boxes WHERE minx <= 0.5 AND maxx >= 0.5) = 1 THEN 'ok'
    ELSE 'FAIL' END;

SELECT 'geopoly: ' || CASE
    WHEN geopoly_area('[[0,0],[1,0],[1,1],[0,1],[0,0]]') = 1.0 THEN 'ok'
    ELSE 'FAIL' END;

SELECT 'math functions: ' || CASE
    WHEN abs(ln(exp(1.0)) - 1.0) < 1e-9 THEN 'ok'
    ELSE 'FAIL' END;

SELECT 'json: ' || CASE
    WHEN json_extract('{"a":{"b":42}}', '$.a.b') = 42 THEN 'ok'
    ELSE 'FAIL' END;

SELECT 'dbstat vtab: ' || CASE
    WHEN (SELECT count(*) FROM dbstat) > 0 THEN 'ok'
    ELSE 'FAIL' END;

-- WAL: режим, в котором hermes держит state.db
.output /dev/null
PRAGMA journal_mode = WAL;
.output stdout
SELECT 'wal: ' || CASE
    WHEN (SELECT * FROM pragma_journal_mode()) = 'wal' THEN 'ok'
    ELSE 'FAIL' END;

-- sessions API из SQL не вызывается, проверяем только флаг сборки
SELECT 'session: ' || CASE
    WHEN (SELECT count(*) FROM pragma_compile_options() WHERE compile_options = 'ENABLE_SESSION') = 1 THEN 'ok'
    ELSE 'FAIL' END;

SELECT 'threadsafe: ' || CASE
    WHEN (SELECT count(*) FROM pragma_compile_options() WHERE compile_options = 'THREADSAFE=1') = 1 THEN 'ok'
    ELSE 'FAIL' END;

SELECT 'max variable number: ' || CASE
    WHEN (SELECT count(*) FROM pragma_compile_options() WHERE compile_options = 'MAX_VARIABLE_NUMBER=250000') = 1 THEN 'ok'
    ELSE 'FAIL' END;

.exit
