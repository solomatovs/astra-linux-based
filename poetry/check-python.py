#!/usr/bin/env python3
"""Сверка requires-python релиза poetry с интерпретатором, которым его собирают.

Запускается на шаге `make deps` ВНУТРИ того самого образа python, который указан
в DEPS_<версия> — то есть проверяет ровно тот интерпретатор, что будет и в сборке,
и в рантайме.

Зачем отдельная проверка: pip при несовпадении не падает, а молча резолвит
СТАРУЮ версию poetry, которая под этот python подходит. Обнаруживается это уже
на собранном образе (`poetry --version` показывает не то, что просили), поэтому
дешевле спросить pypi заранее.

usage: check-python.py <pypi-json-url> <package> <version> [<image-name>]
"""

from __future__ import annotations

import json
import sys
import urllib.request

# packaging тащим из pip: отдельным пакетом его в образе python нет,
# а вендоренная в pip копия есть всегда
from pip._vendor.packaging.specifiers import SpecifierSet
from pip._vendor.packaging.version import Version


def main() -> int:
    base_url, package, version = sys.argv[1], sys.argv[2], sys.argv[3]
    image = sys.argv[4] if len(sys.argv) > 4 else "(текущий интерпретатор)"

    url = f"{base_url}/{package}/{version}/json"
    with urllib.request.urlopen(url, timeout=30) as response:
        meta = json.load(response)["info"]

    requires = meta.get("requires_python") or ""
    python = ".".join(str(part) for part in sys.version_info[:3])

    print(f"{package} {version}:")
    print(f"  requires-python:  {requires or '(не объявлен)'}")
    print(f"  {image}: python {python}")

    if not requires:
        print("  OK: ограничения нет")
        return 0

    if Version(python) in SpecifierSet(requires, prereleases=True):
        print(f"  OK: python {python} подходит под {requires}")
        return 0

    print(f"  FAIL: python {python} не подходит под {requires} —")
    print("        поправьте DEPS_%s в Makefile (другой dmp/python)" % version)
    return 1


if __name__ == "__main__":
    raise SystemExit(main())
