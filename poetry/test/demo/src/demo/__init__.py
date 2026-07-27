"""Пакет-пустышка: нужен, чтобы poetry build собрал колесо и sdist."""

__version__ = "0.1.0"


def greet(name: str = "dmp") -> str:
    return f"hello, {name}"
