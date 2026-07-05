#!/usr/local/bin/python3
"""Проверка работоспособности собранного Python и его C-расширений:
импорт модулей + функциональные тесты (компрессия, крипто, сеть, БД, ffi)."""
import sys

failures = []


def check(name, fn):
    try:
        fn()
        print(f"  OK   {name}")
    except Exception as e:  # noqa: BLE001 - тест, ловим всё намеренно
        print(f"  FAIL {name}: {e!r}")
        failures.append(name)


print("== interpreter ==")
print(f"  python     : {sys.version.split()[0]}")
print(f"  executable : {sys.executable}")

# 1) импорт C-расширений (падают, если при сборке не было нужных dev-библиотек)
print("== import C-extension modules ==")
import importlib

for m in [
    "zlib", "bz2", "lzma", "ssl", "hashlib", "ctypes", "ctypes.util",
    "sqlite3", "socket", "select", "decimal", "uuid", "struct", "array",
    "mmap", "fcntl", "termios", "unicodedata", "readline", "curses",
    "_multiprocessing", "json",
]:
    check(f"import {m}", lambda m=m: importlib.import_module(m))

# 2) компрессия — round-trip (zlib/bz2/lzma/gzip)
print("== compression ==")
import io, zlib, bz2, lzma, gzip

data = b"hello astra-linux " * 2000


def t_zlib():
    assert zlib.decompress(zlib.compress(data, 9)) == data


def t_bz2():
    assert bz2.decompress(bz2.compress(data, 9)) == data


def t_lzma():
    assert lzma.decompress(lzma.compress(data)) == data


def t_gzip():
    buf = io.BytesIO()
    with gzip.GzipFile(fileobj=buf, mode="wb") as f:
        f.write(data)
    assert gzip.GzipFile(fileobj=io.BytesIO(buf.getvalue())).read() == data


check("zlib  round-trip", t_zlib)
check("bz2   round-trip", t_bz2)
check("lzma  round-trip", t_lzma)
check("gzip  round-trip", t_gzip)

# 3) hashlib (в т.ч. алгоритмы из OpenSSL)
print("== hashlib ==")
import hashlib


def t_sha256():
    assert (hashlib.sha256(b"abc").hexdigest()
            == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad")


check("sha256 known vector", t_sha256)
check("sha3_256 (OpenSSL)", lambda: hashlib.sha3_256(b"abc").hexdigest())
check("blake2b", lambda: hashlib.blake2b(b"abc").hexdigest())

# 4) ssl — доказывает, что _ssl слинкован с libssl/libcrypto
print("== ssl ==")
import ssl

check("OpenSSL version", lambda: print(f"       {ssl.OPENSSL_VERSION}"))
check("create_default_context", ssl.create_default_context)

# 5) сеть — loopback TCP (без интернета) + резолв
print("== network ==")
import socket, threading


def t_tcp_loopback():
    srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    srv.bind(("127.0.0.1", 0))
    srv.listen(1)
    port = srv.getsockname()[1]
    got = {}

    def serve():
        conn, _ = srv.accept()
        got["msg"] = conn.recv(64)
        conn.sendall(b"pong")
        conn.close()

    t = threading.Thread(target=serve)
    t.start()
    cli = socket.create_connection(("127.0.0.1", port), timeout=5)
    cli.sendall(b"ping")
    resp = cli.recv(64)
    cli.close()
    t.join(timeout=5)
    srv.close()
    assert got.get("msg") == b"ping" and resp == b"pong"


check("TCP send/recv over 127.0.0.1", t_tcp_loopback)
check("resolve localhost", lambda: socket.gethostbyname("localhost"))

# 6) sqlite3
print("== sqlite3 ==")
import sqlite3


def t_sqlite():
    con = sqlite3.connect(":memory:")
    con.execute("create table t(x int)")
    con.executemany("insert into t values(?)", [(i,) for i in range(10)])
    (s,) = con.execute("select sum(x) from t").fetchone()
    con.close()
    assert s == 45


check("in-memory db + query", t_sqlite)

# 7) ctypes / libffi
print("== ctypes / libffi ==")
import ctypes, ctypes.util


def t_ctypes():
    libc = ctypes.CDLL(ctypes.util.find_library("c") or "libc.so.6")
    assert libc.getpid() > 0


check("load libc + call getpid", t_ctypes)

# 8) прочее
print("== misc ==")
import json, decimal, uuid


def t_json():
    assert json.loads(json.dumps({"a": [1, 2, 3]}))["a"] == [1, 2, 3]


def t_decimal():
    assert str(decimal.Decimal("0.1") + decimal.Decimal("0.2")) == "0.3"


def t_uuid():
    assert uuid.uuid4().version == 4


check("json round-trip", t_json)
check("decimal 0.1+0.2==0.3", t_decimal)
check("uuid4", t_uuid)

print()
if failures:
    print(f"RESULT: FAIL ({len(failures)}): {', '.join(failures)}")
    sys.exit(1)
print("RESULT: ALL OK")
