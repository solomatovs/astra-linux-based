# poetry
ARG RUNTIME_IMAGE=dmp/python:3.13

FROM ${RUNTIME_IMAGE} AS base
FROM base AS builder

ARG POETRY_VERSION
ARG PYTHON_TAG
# staging-каталог (как DESTDIR у остальных проектов): его содержимое
# раскладывается в корень рантайма
ARG POETRY_PATH=/opt/poetry
# venv, в котором живёт сам poetry. Ставить его в системные site-packages нельзя:
# poetry управляет чужими окружениями и не должен делить их со своими зависимостями
# (его собственные пины cleo/requests/virtualenv тогда становятся пинами проекта).
# Путь СРАЗУ конечный: в venv зашиты абсолютные пути (pyvenv.cfg, шебанги
# console_scripts), поэтому собрать его в /opt/poetry/... и «переехать» нельзя —
# сначала собираем по месту, потом копируем в staging
ARG POETRY_VENV=/usr/local/lib/poetry

# завендоренные колёса: всё дерево зависимостей + install.txt (что ставить)
# + constraints.txt (пины всего, что приехало). Собирается `make sources`
COPY artifacts/src/poetry-${POETRY_VERSION}-python${PYTHON_TAG}.tar.gz /tmp/wheelhouse.tar.gz
COPY glibc-floor.py /tmp/glibc-floor.py

# сеть в сборке не нужна и не используется: --no-index закрывает pip наглухо,
# --find-links оставляет ему единственный источник — распакованный wheelhouse
RUN set -eux;                                                                   \
    tar -xf /tmp/wheelhouse.tar.gz -C /tmp;                                     \
    python3 /tmp/glibc-floor.py /tmp/wheelhouse                                 \
        ${POETRY_PATH}/usr/local/share/poetry/glibc-floor.txt;                  \
    python3 -m venv ${POETRY_VENV};                                             \
    ${POETRY_VENV}/bin/pip install                                              \
        --no-index --no-cache-dir --disable-pip-version-check                   \
        --find-links /tmp/wheelhouse                                            \
        --requirement /tmp/wheelhouse/install.txt                               \
        --constraint  /tmp/wheelhouse/constraints.txt;                          \
    # проверяем сразу здесь: дальше идёт только копирование файлов
    ${POETRY_VENV}/bin/poetry --version | grep -F "${POETRY_VERSION}";          \
    mkdir -p ${POETRY_PATH}/usr/local/bin ${POETRY_PATH}/usr/local/lib;         \
    cp -a ${POETRY_VENV} ${POETRY_PATH}/usr/local/lib/;                         \
    # относительная ссылка: /usr/local/bin/poetry -> /usr/local/lib/poetry/bin/poetry
    ln -s ../lib/poetry/bin/poetry ${POETRY_PATH}/usr/local/bin/poetry;         \
    cp /tmp/wheelhouse/constraints.txt                                          \
        ${POETRY_PATH}/usr/local/share/poetry/wheels.txt;                       \
    cp /tmp/wheelhouse/install.txt                                              \
        ${POETRY_PATH}/usr/local/share/poetry/install.txt;                      \
    du -h --max-depth=1 ${POETRY_PATH}/usr/local;                               \
    cd /; rm -rf /tmp/*


# dist: только собранный /opt/poetry
FROM base AS dist
ARG POETRY_PATH=/opt/poetry
COPY --from=builder ${POETRY_PATH} ${POETRY_PATH}


FROM base AS runtime

ARG POETRY_PATH=/opt/poetry
ARG POETRY_VERSION

# устанавливаем собранный poetry в runtime-образ
COPY --from=dist ${POETRY_PATH} /

# corp nexus: глобальный конфиг poetry (репозитории, virtualenvs) и логин/пароль
# к ним. Реальные файлы — poetry-config.toml и poetry-auth.toml рядом с Dockerfile
# (в .gitignore), см. *.example. По умолчанию их нет — poetry ходит в pypi.org.
# Маска *.toml* всегда цепляет .example, поэтому COPY не падает на пустом glob
COPY poetry-config.toml* poetry-auth.toml* /tmp/pconf/
RUN set -eux;                                                                   \
    mkdir -p /etc/pypoetry;                                                     \
    if [ -f /tmp/pconf/poetry-config.toml ]; then                               \
        cp /tmp/pconf/poetry-config.toml /etc/pypoetry/config.toml;             \
    fi;                                                                         \
    if [ -f /tmp/pconf/poetry-auth.toml ]; then                                 \
        cp /tmp/pconf/poetry-auth.toml /etc/pypoetry/auth.toml;                 \
        chmod 600 /etc/pypoetry/auth.toml;                                      \
    fi;                                                                         \
    rm -rf /tmp/pconf

# POETRY_CONFIG_DIR — poetry читает отсюда config.toml и auth.toml (по умолчанию
# это ~/.config/pypoetry, то есть у каждого пользователя свой; в образе конфиг
# общий и не зависит от того, под кем контейнер запущен).
# VIRTUALENV_NO_PERIODIC_UPDATE — virtualenv (им poetry создаёт окружения проектов)
# фоном ходит на pypi за свежими pip/setuptools; в закрытом контуре это лишний
# стук наружу и лишние секунды на каждом `poetry install`
ENV POETRY_CONFIG_DIR=/etc/pypoetry \
    VIRTUALENV_NO_PERIODIC_UPDATE=1 \
    PATH=/usr/local/bin:${PATH}

LABEL org.opencontainers.image.title="poetry" \
      org.opencontainers.image.version="${POETRY_VERSION}" \
      org.opencontainers.image.source="https://github.com/python-poetry/poetry"

WORKDIR /workspace

ENTRYPOINT [ "/usr/local/bin/poetry" ]
