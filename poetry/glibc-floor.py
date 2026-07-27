#!/usr/bin/env python3
"""Порог переносимости образа: минимальный glibc, при котором заработают колёса.

У остальных проектов контура порог считается по бинарю (`objdump -T | GLIBC_*`),
здесь считать нечего: poetry — чистый python, но в его дереве зависимостей есть
колёса с C-расширениями (cryptography, rapidfuzz, dulwich, msgpack, cffi), и они
приезжают готовыми, собранными не нами. Требуемый glibc такое колесо объявляет
своим platform tag: manylinux_2_28_x86_64 -> нужен glibc >= 2.28.

Порог образа = максимум по колёсам от (минимума по platform-тегам одного колеса:
теги в имени перечислены через точку, достаточно любого из них).

Скрипт и печатает порог, и падает, если он выше glibc, на котором собран образ —
иначе несовместимость всплывёт не здесь, а импортом .so в рантайме у пользователя.

usage: glibc-floor.py <wheelhouse-dir> <output-file>
"""

from __future__ import annotations

import os
import pathlib
import re
import sys

# устаревшие псевдонимы manylinux (PEP 513/571/599)
LEGACY = {
    "manylinux1": (2, 5),
    "manylinux2010": (2, 12),
    "manylinux2014": (2, 17),
}
MANYLINUX_RE = re.compile(r"^manylinux_(\d+)_(\d+)_")


def tag_floor(tag: str) -> tuple[int, int] | str:
    """Что требует один platform tag: версию glibc, "free" или "other"."""
    for name, version in LEGACY.items():
        if tag.startswith(name + "_"):
            return version
    match = MANYLINUX_RE.match(tag)
    if match:
        return int(match.group(1)), int(match.group(2))
    if tag == "any" or tag.startswith("linux_"):
        # чистый python или колесо, собранное на месте — к glibc не привязано
        return "free"
    # musllinux/macosx/win: на glibc-рантайме такой тег просто не выбирается
    return "other"


def wheel_floor(name: str) -> tuple[int, int] | None:
    """glibc, который требует колесо: минимум по его glibc-тегам."""
    floors = []
    for tag in name[: -len(".whl")].split("-")[-1].split("."):
        required = tag_floor(tag)
        if required == "free":
            return None
        if required != "other":
            floors.append(required)
    return min(floors) if floors else None


def system_glibc() -> tuple[int, int]:
    # 'glibc 2.28' -> (2, 28)
    version = (os.confstr("CS_GNU_LIBC_VERSION") or "glibc 0.0").split()[-1]
    major, _, minor = version.partition(".")
    return int(major), int(minor.split(".")[0] or 0)


def main() -> int:
    wheelhouse, output = pathlib.Path(sys.argv[1]), pathlib.Path(sys.argv[2])

    lines: list[str] = []
    floor = (0, 0)
    for wheel in sorted(wheelhouse.glob("*.whl")):
        required = wheel_floor(wheel.name)
        if not required:
            continue
        floor = max(floor, required)
        lines.append("%-40s GLIBC_%d.%d" % (wheel.name.split("-")[0], *required))

    current = system_glibc()
    header = [
        "glibc floor: %d.%d" % floor,
        "образ собран на glibc %d.%d" % current,
        "",
        "колёса с C-расширениями:",
    ]
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text("\n".join(header + lines) + "\n")
    print(output.read_text(), end="")

    if floor > current:
        print(
            "FAIL: колёса требуют glibc %d.%d, а рантайм — %d.%d.\n"
            "      апстрим перевёл сборку колёс на более новый manylinux;\n"
            "      варианты: закрепить версии пакета пониже, поднять dmp/glibc\n"
            "      или собрать эти пакеты из sdist (NO_BINARY в Makefile)" % (*floor, *current),
            file=sys.stderr,
        )
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
