"""Проверка, что python видит подложенную libsqlite3, а не системную."""

import sqlite3
import sys

MIN_VERSION = (3, 51, 3)

if sqlite3.sqlite_version_info < MIN_VERSION:
    sys.exit(
        f"  FAIL  sqlite {sqlite3.sqlite_version}: ниже "
        f"{'.'.join(map(str, MIN_VERSION))}, WAL-reset баг не исправлен"
    )

db = sqlite3.connect(":memory:")
db.execute("CREATE VIRTUAL TABLE d USING fts5(c, tokenize='trigram')")
db.execute("INSERT INTO d VALUES ('hermes')")
if db.execute("SELECT count(*) FROM d WHERE d MATCH 'erm'").fetchone()[0] != 1:
    sys.exit("  FAIL  fts5 trigram не работает")

db.execute("PRAGMA journal_mode = WAL")
db.close()

print(f"  ok    sqlite {sqlite3.sqlite_version}, fts5 trigram, wal")
