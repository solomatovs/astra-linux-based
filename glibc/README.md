# astra-linux

Сборка базового Docker-образа на основе Astra Linux (Orel) с пересобранной из исходников **glibc 2.28** и русской локалью `ru_RU.UTF-8`. Нужен, чтобы запускать на Astra бинарники, требующие более новый glibc, чем в штатном образе.

## Команды

| Команда | Что делает |
|---------|------------|
| `make help` | список команд (по умолчанию) |
| `make sources` | скачать исходники glibc в `artifacts/glibc-src/`; напоминает положить tar базового образа astra в `artifacts/` |
| `make debs` | скачать .deb (`apt-transport-https` и т.п.) через `docker run` orel в `artifacts/apt-bootstrap/` |
| `make build` | собрать образ `astra-linux:<ASTRA_VERSION>-glibc<GLIBC_VERSION>` (цель `glibc` из `Dockerfile`) |

## Закрытый контур / https-репозиторий

В базовом образе orel нет драйвера `/usr/lib/apt/methods/https`, поэтому если
`apt-sources.list` ведёт на `https://` (или http редиректит на https), `apt-get`
падает с `The method driver /usr/lib/apt/methods/https could not be found`.

Решение — заранее скачать `apt-transport-https` и положить рядом:

```sh
make debs
```

`.deb` попадут в `artifacts/apt-bootstrap/`, а `Dockerfile` поставит их через
`dpkg -i` до первого `apt-get update`. Переопределяемые переменные:
`DEB_PACKAGES` (список пакетов), `APT_DEB_DIR`.

## Переменные

`ASTRA_VERSION` (2.12), `GLIBC_VERSION` (2.28), `ARTIFACTS` (`artifacts`), `IMAGE`, `BUILD_CONTEXT` — переопределяются при вызове, напр.:

```sh
make build GLIBC_VERSION=2.31
```
