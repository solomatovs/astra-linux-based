# poetry

Сборка [python-poetry/poetry](https://github.com/python-poetry/poetry) по шаблону
родственных проектов (`Makefile` + `Dockerfile` + `test`, цепочка версий,
`sources`/`dist`/`build`/`test`).

Результат — образы `dmp/poetry:<версия>` и `dmp/poetry:<версия>-dist` поверх
соответствующего `dmp/python`, с `poetry` в `/usr/local/bin`.

```
make all                  # всё: sources -> dist -> build -> test
make all VERSION=1.8.5    # другая версия из цепочки
make deps                 # сверить requires-python релиза с python из DEPS (нужна сеть)
make wheels               # пины всех колёс, из которых собран образ
make glibc                # порог переносимости колёс образа
```

Использование:

```
docker run --rm -v "$PWD":/workspace dmp/poetry:2 install
docker run --rm -v "$PWD":/workspace dmp/poetry:2 build
docker run --rm -v "$PWD":/workspace --entrypoint sh dmp/poetry:2   # интерактивно
```

## Что здесь «компилируется»

Poetry — чистый python, компилировать в привычном смысле нечего. Сборка из
исходников тут ровно одна: `poetry` и `poetry-core` собираются из sdist
(`NO_BINARY=poetry,poetry-core` в `Makefile`) — оба на бэкенде `poetry-core`,
занимает секунды.

Остальное дерево (44 пакета) берётся готовыми колёсами. Собирать из sdist и его
означало бы затащить в контур то, чего в тулчейне нет:

* `cryptography` (через `keyring` -> `SecretStorage`) требует rust и `setuptools-rust`;
* `rapidfuzz` (через `cleo`) требует C++17 и cmake, а базовый компилятор контура — gcc 8.5.0;
* `dulwich`, `msgpack`, `cffi`, `charset-normalizer`, `backports.zstd` — C-расширения.

Полностью из исходников: `make all NO_BINARY=:all:`, но тогда нужен свой
build-образ с `cc` и `rustc` (в `dmp/python` компилятора нет).

## Оффлайн-сборка

Сеть нужна ровно на одном шаге — `make sources`. Он поднимает контейнер
`dmp/python` (обычная docker-сеть, ничего особенного ему не нужно) и кладёт в
`$(ARTIFACTS)/src/poetry-<версия>-python<тег>.tar.gz` (17–24 МБ):

* `wheelhouse/*.whl` — всё дерево зависимостей, посчитанное `pip wheel`
  (для 2.4.1 это 45 колёс);
* `wheelhouse/install.txt` — что ставить (`poetry==<версия>` + `PLUGINS`);
* `wheelhouse/constraints.txt` — пины всего, что приехало; они же лежат в образе
  как `/usr/local/share/poetry/wheels.txt` (`make wheels`).

Дальше `dist`/`build` идут без сети: `pip install --no-index --find-links`
закрывает pip наглухо, единственный источник — распакованный wheelhouse.

Проверяется это не на слово: `make test` запускает контейнер с `--network none`
и прогоняет в нём `poetry check / lock / build / install --no-root` на демо-проекте
из `test/demo`.

Отдельная граната — `virtualenv`: он фоном ходит на pypi за свежими
`pip`/`setuptools` для своих seed-окружений. В образе выставлен
`VIRTUALENV_NO_PERIODIC_UPDATE=1`, иначе каждый `poetry install` в закрытом
контуре ждёт таймаута.

## poetry живёт в отдельном venv

`poetry` ставится не в системные `site-packages`, а в venv `/usr/local/lib/poetry`,
наружу торчит только симлинк `/usr/local/bin/poetry`. Иначе его собственные пины
(`cleo`, `requests`, `virtualenv`, `cryptography`) оказались бы в том же окружении,
которым пользуется проект.

```
$ docker run --rm --entrypoint python3 dmp/poetry:2 -c "import cleo"
ModuleNotFoundError: No module named 'cleo'
```

Venv собирается сразу по конечному пути и только потом копируется в staging
`/opt/poetry`: внутри venv абсолютные пути (`pyvenv.cfg`, шебанги console_scripts),
собрать его «где-то» и переехать нельзя.

Плагины ставятся в тот же venv (`PLUGINS` в `Makefile`, оффлайн-аналог
`poetry self add`). По умолчанию — `poetry-plugin-export`: в закрытом контуре
`poetry export -f requirements.txt` это основной мост к `pip`.

## Привязка к образу python

`DEPS_<версия>` здесь одно слово, а не пара `runtime_image`/`build_image`, как у
остальных проектов: образ сборки и образ рантайма обязаны совпадать.

* в venv зашиты абсолютные пути до `/usr/local/bin/python3`;
* колёса с C-расширениями собраны под конкретный ABI — `cp313` в `cp311` не поедет.

| версия poetry | requires-python | образ           |
|---------------|-----------------|-----------------|
| 1.8.5         | >=3.8,<4.0      | `dmp/python:3.11` |
| 2.1.4         | >=3.9,<4.0      | `dmp/python:3.13` |
| 2.4.1         | >=3.10,<4.0     | `dmp/python:3.13` |

1.8.5 оставлен на 3.11 не из вкусовщины: на cp313 у его пина `dulwich 0.21.x`
колёс уже нет, и `pip wheel` уходит собирать его из sdist.

`make deps` сверяет `requires-python` релиза с python из `DEPS_<версия>`. Проверка
нужна ДО вендоринга, потому что при несовпадении pip не падает, а молча резолвит
старую версию poetry, которая под этот python подходит — обнаруживается это уже
на собранном образе.

Другой python для всей цепочки: `make all PYTHON_IMAGE=dmp/python:3.12`
(имя тарбола содержит тег python, так что артефакты разных питонов не смешиваются).

## Минимальный glibc

У остальных проектов порог переносимости считается по своему бинарю
(`objdump -T | GLIBC_*`). Здесь считать нечего — свой код чистый python, зато
приезжают чужие колёса с C-расширениями, и требуемый glibc они объявляют
platform-тегом: `manylinux_2_28_x86_64` -> нужен glibc >= 2.28.

`glibc-floor.py` считает на этапе builder максимум по колёсам (для каждого
колеса — минимум по его тегам, их в имени может быть несколько), кладёт результат
в `/usr/local/share/poetry/glibc-floor.txt` и падает, если порог выше glibc
рантайма. Показать: `make glibc`.

```
glibc floor: 2.28
образ собран на glibc 2.28

колёса с C-расширениями:
cryptography    GLIBC_2.28
dulwich         GLIBC_2.28
rapidfuzz       GLIBC_2.27
msgpack         GLIBC_2.17
...
```

Запас нулевой: `cryptography` и `dulwich` уже собираются под `manylinux_2_28`, то
есть ровно под нижнюю границу контура (Astra Orel 2.12, glibc 2.28). Когда апстрим
уедет на `manylinux_2_34`, сборка упадёт на этой проверке, а не импортом `.so` у
пользователя. Варианты на тот момент: закрепить версию пакета пониже, поднять
`dmp/glibc` или собрать конкретный пакет из sdist через `NO_BINARY`.

## Закрытый контур

Индекс, из которого вендорятся колёса, задаётся файлом `pip.conf` рядом с
`Makefile` (см. `pip.conf.example`) — тот же формат, что у `python/3/pip.conf`
соседнего проекта. Если файл есть, он монтируется в контейнер `sources` как
`/etc/pip.conf`; в образ он не попадает — сборка идёт с `--no-index`, а рантайму
он не нужен. Через него же настраиваются `trusted-host`, сертификат, прокси и
креды к nexus.

Быстрая альтернатива на один прогон — `make sources INDEX_URL=...`. Задавать
что-то одно: аргумент командной строки старше конфига, поэтому по умолчанию
`INDEX_URL` пустой.

Дальше рантайм. `POETRY_CONFIG_DIR=/etc/pypoetry` — общий конфиг образа, не
зависящий от того, под каким пользователем запущен контейнер:

* `poetry-config.toml.example` -> реальный файл `poetry-config.toml` -> `/etc/pypoetry/config.toml`
  (`virtualenvs.in-project`, `keyring.enabled`, репозитории для publish);
* `poetry-auth.toml.example` -> `poetry-auth.toml` -> `/etc/pypoetry/auth.toml` (права 600).

Оба реальных файла в `.gitignore`; по умолчанию их нет — poetry ходит в pypi.org.

Важно: poetry **не читает** `pip.conf` и не имеет глобальной настройки индекса.
Зеркало задаётся в самом проекте:

```toml
[[tool.poetry.source]]
name = "nexus"
url = "https://nexus.example.com/repository/pypi/simple/"
priority = "primary"
```

а учётные данные — либо в `auth.toml`, либо переменными окружения
`POETRY_HTTP_BASIC_NEXUS_USERNAME` / `POETRY_HTTP_BASIC_NEXUS_PASSWORD` на запуске
контейнера (пароль в `auth.toml` лежит открытым текстом в слое образа).

## Тест

`make test` гоняет образ с `--network none` на `test/demo` — минимальном пакете
без зависимостей (любая зависимость потребовала бы индекса). Проверяются
`poetry check`, `lock`, `build` (собираются sdist и wheel), `install --no-root`
(создание venv через virtualenv) и `export`.

`pyproject.toml` демо намеренно в старом формате `[tool.poetry]`: тест один на всю
цепочку, а PEP 621 `[project]` понимает только poetry 2.x. Предупреждения о
deprecated-ключах в выводе 2.x — ожидаемые.
