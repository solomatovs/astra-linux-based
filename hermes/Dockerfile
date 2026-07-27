# hermes-agent
ARG RUNTIME_IMAGE=dmp/nodejs:22.22.1
ARG PYTHON_IMAGE=dmp/python:3.13-dist
ARG GCC_IMAGE=dmp/gcc:12.4.0-dist
ARG UV_IMAGE=dmp/uv:0.9.26-dist
ARG SQLITE_IMAGE=dmp/sqlite:3.53.4-dist
ARG CMAKE_IMAGE=dmp/cmake:3.27.9-dist

FROM ${GCC_IMAGE}       AS gcc
FROM ${PYTHON_IMAGE}    AS python
FROM ${UV_IMAGE}        AS uv
FROM ${SQLITE_IMAGE}    AS sqlite
FROM ${CMAKE_IMAGE}     AS cmake
FROM ${RUNTIME_IMAGE}   AS base


# libolm: пакета в репозиториях astra нет, а без него не ставится python-olm
# из matrix-extra (mautrix[encryption])
FROM base AS olm

ARG OLM_VERSION
ARG OLM_PATH=/opt/olm
ARG GCC_PATH=/opt/gcc
ARG CMAKE_PATH=/opt/cmake

COPY --from=gcc     ${GCC_PATH}/usr/local/      /usr/local/
COPY --from=gcc     /usr/include/               /usr/include/
COPY --from=cmake   ${CMAKE_PATH}/usr/local/    /usr/local/
RUN ln -sf gcc /usr/local/bin/cc            \
    && ln -sf g++ /usr/local/bin/c++        \
    && printf '%s\n'                        \
        /usr/local/lib                      \
        /usr/local/lib64                    \
        > /etc/ld.so.conf.d/000-local.conf  \
    && ldconfig

COPY artifacts/src/olm-${OLM_VERSION}.tar.gz /tmp/src/olm.tar.gz

RUN set -eux;                                                   \
    mkdir -p /tmp/build;                                        \
    tar -xf /tmp/src/olm.tar.gz -C /tmp/build;                  \
    cmake -S /tmp/build/olm-${OLM_VERSION} -B /tmp/build/out    \
        -DCMAKE_BUILD_TYPE=Release                              \
        -DCMAKE_INSTALL_PREFIX=/usr/local                       \
        -DBUILD_SHARED_LIBS=ON                                  \
        -DOLM_TESTS=OFF;                                        \
    cmake --build /tmp/build/out -j"$(nproc)";                  \
    cmake --install /tmp/build/out --strip;                     \
    DESTDIR=${OLM_PATH} cmake --install /tmp/build/out --strip; \
    ldconfig;                                                   \
    du -h --max-depth=3 ${OLM_PATH};                            \
    rm -rf /tmp/build /tmp/src


FROM base AS builder

ARG HERMES_VERSION
ARG HERMES_PATH=/opt/hermes
ARG UV_EXTRAS
ARG WITH_PLAYWRIGHT=0
ARG PLAYWRIGHT_PLATFORM
ARG GCC_PATH=/opt/gcc
ARG PYTHON_PATH=/opt/python
ARG UV_PATH=/opt/uv
ARG SQLITE_PATH=/opt/sqlite
ARG OLM_PATH=/opt/olm

# тулчейн, интерпретатор и uv из -dist образов.
# libolm с заголовками нужен здесь: python-olm ставится из sdist и собирает
# cffi-биндинг на месте
COPY --from=gcc     ${GCC_PATH}/usr/local/       /usr/local/
COPY --from=gcc     /usr/include/                /usr/include/
COPY --from=python  ${PYTHON_PATH}/usr/local/    /usr/local/
COPY --from=uv      ${UV_PATH}/usr/local/        /usr/local/
COPY --from=sqlite  ${SQLITE_PATH}/usr/local/    /usr/local/
COPY --from=olm     ${OLM_PATH}/usr/local/       /usr/local/
RUN ln -sf gcc /usr/local/bin/cc            \
    && ln -sf g++ /usr/local/bin/c++        \
    && printf '%s\n'                        \
        /usr/local/lib                      \
        /usr/local/lib64                    \
        > /etc/ld.so.conf.d/000-local.conf  \
    && ldconfig

RUN apt-get update                                              \
    && apt-get install -y --no-install-recommends               \
        git ca-certificates curl xz-utils                       \
        zlib1g-dev libffi-dev libssl-dev                        \
    && apt-get clean                                            \
    && rm -rf /var/lib/apt/lists/* /var/cache/apt/archives/*

# конфиги реестров для закрытого контура (nexus). файлов может не быть —
# тогда npm и uv идут в registry.npmjs.org и pypi.org
COPY artifacts/npmrc* /tmp/reg/
COPY artifacts/uv-config.toml* /tmp/reg/
RUN if [ -f /tmp/reg/npmrc ]; then cp /tmp/reg/npmrc /usr/local/etc/npmrc; fi \
    && if [ -f /tmp/reg/uv-config.toml ]; then                                \
        mkdir -p /etc/uv && cp /tmp/reg/uv-config.toml /etc/uv/uv.toml;       \
    fi                                                                        \
    && rm -rf /tmp/reg

ENV NODE_EXTRA_CA_CERTS=/etc/ssl/certs/ca-certificates.crt
# интерпретатор берём из образа, скачивать python-build-standalone запрещаем
ENV UV_PYTHON=/usr/local/bin/python3
ENV UV_PYTHON_DOWNLOADS=never
ENV PYTHONDONTWRITEBYTECODE=1
# file:-зависимости монорепы ставятся симлинками, а не копиями
ENV npm_config_install_links=false

COPY artifacts/src/hermes-agent-${HERMES_VERSION}-src.tar.gz /tmp/src/hermes.tar.gz

RUN set -eux;                                                   \
    mkdir -p ${HERMES_PATH};                                    \
    tar -xf /tmp/src/hermes.tar.gz -C ${HERMES_PATH} --strip-components=1; \
    rm -rf /tmp/src

WORKDIR ${HERMES_PATH}

# фронты: dashboard (web/) и TUI (ui-tui/). оба бандла нужны в рантайме,
# иначе launcher уходит в `npm install` на каждом старте
RUN set -eux;                                                   \
    npm install --prefer-offline --no-audit --fetch-retries=5;  \
    if [ "${WITH_PLAYWRIGHT}" = "1" ]; then                     \
        PLAYWRIGHT_HOST_PLATFORM_OVERRIDE=${PLAYWRIGHT_PLATFORM} \
            npx --yes playwright install chromium;              \
    fi;                                                         \
    cd web && npm run build;                                    \
    cd ../ui-tui && npm run build;                              \
    npm cache clean --force

# --frozen: состав ставится строго по uv.lock, extra перечислены в UV_EXTRAS
RUN set -eux;                                                   \
    extras="";                                                  \
    for e in ${UV_EXTRAS}; do extras="$extras --extra $e"; done; \
    uv sync --frozen --no-install-project $extras;              \
    uv pip install --python ${HERMES_PATH}/.venv/bin/python     \
        --no-cache-dir --no-deps -e ".";                        \
    test -x ${HERMES_PATH}/.venv/bin/hermes;                    \
    uv cache clean

# shim подменяет hermes при `docker exec` от root: сбрасывает привилегии
# на пользователя hermes, иначе записанные файлы недоступны гейтвею
RUN set -eux;                                                   \
    mkdir -p ${HERMES_PATH}/bin;                                \
    cp ${HERMES_PATH}/docker/hermes-exec-shim.sh ${HERMES_PATH}/bin/hermes; \
    chmod 0755 ${HERMES_PATH}/bin/hermes;                       \
    printf 'docker\n' > ${HERMES_PATH}/.install_method;         \
    printf '%s\n' "${HERMES_VERSION}" > ${HERMES_PATH}/.hermes_build_sha; \
    chmod -R a+rX,go-w ${HERMES_PATH};                          \
    du -h --max-depth=1 ${HERMES_PATH}


# dist: только собранное дерево /opt/hermes без сборочного тулчейна
FROM base AS dist
ARG HERMES_PATH=/opt/hermes
COPY --from=builder ${HERMES_PATH} ${HERMES_PATH}


FROM base AS runtime

ARG HERMES_PATH=/opt/hermes
ARG GCC_PATH=/opt/gcc
ARG PYTHON_PATH=/opt/python
ARG UV_PATH=/opt/uv
ARG SQLITE_PATH=/opt/sqlite
ARG OLM_PATH=/opt/olm
ARG S6_VERSION
ARG RIPGREP_VERSION
ARG RIPGREP_TARGET
ARG WITH_CHROMIUM=1
ARG HERMES_UID=10000

# интерпретатор, uv, libsqlite3 с FTS5 и libolm; из gcc берём только
# рантайм-библиотеки
COPY --from=python  ${PYTHON_PATH}/usr/local/       /usr/local/
COPY --from=uv      ${UV_PATH}/usr/local/           /usr/local/
COPY --from=sqlite  ${SQLITE_PATH}/usr/local/       /usr/local/
COPY --from=olm     ${OLM_PATH}/usr/local/lib/      /usr/local/lib/
COPY --from=gcc     ${GCC_PATH}/usr/local/lib64/libstdc++.so.6* ${GCC_PATH}/usr/local/lib64/libgcc_s.so.1* /usr/local/lib64/

# /usr/local/lib первым: наша libsqlite3.so.0 должна перекрывать системную
RUN printf '%s\n'                                   \
        /usr/local/lib                              \
        /usr/local/lib64                            \
        > /etc/ld.so.conf.d/000-local.conf          \
    && ldconfig                                     \
    && python3 -c "import sqlite3, sys;              \
v = sqlite3.sqlite_version_info;                     \
sys.exit(f'linked SQLite {sqlite3.sqlite_version} has the WAL-reset bug') if v < (3, 51, 3) else None; \
db = sqlite3.connect(':memory:');                    \
db.execute(\"CREATE VIRTUAL TABLE d USING fts5(c, tokenize='trigram')\")"

RUN apt-get update                                              \
    && apt-get install -y --no-install-recommends               \
        git ca-certificates curl openssh-client procps          \
        xz-utils ffmpeg iputils-ping                            \
    && if [ "${WITH_CHROMIUM}" = "1" ]; then                    \
        apt-get install -y --no-install-recommends chromium;    \
    fi                                                          \
    && apt-get clean                                            \
    && rm -rf /var/lib/apt/lists/* /var/cache/apt/archives/*

# ripgrep: пакета в репозиториях astra нет, ставим prebuilt статический бинарник
COPY artifacts/src/ripgrep-${RIPGREP_VERSION}-${RIPGREP_TARGET}.tar.gz /tmp/src/ripgrep.tar.gz
RUN set -eux;                                                   \
    tar -xzf /tmp/src/ripgrep.tar.gz -C /tmp;                   \
    install -m 0755 /tmp/ripgrep-${RIPGREP_VERSION}-${RIPGREP_TARGET}/rg /usr/local/bin/rg; \
    rm -rf /tmp/src /tmp/ripgrep-${RIPGREP_VERSION}-${RIPGREP_TARGET}

# s6-overlay: /init становится PID 1, он же реапит зомби от stdio-подпроцессов
# MCP и супервизирует гейтвеи профилей
COPY artifacts/src/s6-overlay-${S6_VERSION}-noarch.tar.xz           /tmp/s6/noarch.tar.xz
COPY artifacts/src/s6-overlay-${S6_VERSION}-x86_64.tar.xz           /tmp/s6/arch.tar.xz
COPY artifacts/src/s6-overlay-${S6_VERSION}-symlinks-noarch.tar.xz  /tmp/s6/symlinks.tar.xz
RUN set -eux;                                                   \
    tar -C / -Jxpf /tmp/s6/noarch.tar.xz;                       \
    tar -C / -Jxpf /tmp/s6/arch.tar.xz;                         \
    tar -C / -Jxpf /tmp/s6/symlinks.tar.xz;                     \
    rm -rf /tmp/s6

RUN useradd -u ${HERMES_UID} -m -d /opt/data hermes

COPY --from=dist ${HERMES_PATH} ${HERMES_PATH}

# статические сервисы (main-hermes, dashboard) и cont-init.d хуки лежат в
# исходниках; гейтвеи профилей регистрируются в /run/service уже в рантайме
RUN set -eux;                                                   \
    mkdir -p /etc/s6-overlay/s6-rc.d /etc/cont-init.d;          \
    cp -a ${HERMES_PATH}/docker/s6-rc.d/. /etc/s6-overlay/s6-rc.d/; \
    printf '#!/command/with-contenv sh\nexec %s/docker/stage2-hook.sh\n' "${HERMES_PATH}" \
        > /etc/cont-init.d/01-hermes-setup;                     \
    cp ${HERMES_PATH}/docker/cont-init.d/015-supervise-perms /etc/cont-init.d/015-supervise-perms; \
    cp ${HERMES_PATH}/docker/cont-init.d/02-reconcile-profiles /etc/cont-init.d/02-reconcile-profiles; \
    chmod 0755 /etc/cont-init.d/01-hermes-setup                 \
        /etc/cont-init.d/015-supervise-perms                    \
        /etc/cont-init.d/02-reconcile-profiles;                 \
    cp ${HERMES_PATH}/docker/tini-shim.sh /usr/bin/tini;        \
    chmod 0755 /usr/bin/tini

# agent-browser: обычный бинарь пакета требует GLIBC_2.30, в базовом образе
# столько нет. musl-сборка статическая и работает; кладём её на PATH — hermes
# берёт любой agent-browser, который отвечает на --version (agent_browser_runnable).
# маркер /lib/ld-musl-*.so.1 намеренно не создаём: по нему musl определяют
# также npm и uv, и они начали бы тянуть musl-колёса
RUN set -eux;                                                   \
    install -m 0755                                             \
        ${HERMES_PATH}/node_modules/agent-browser/bin/agent-browser-linux-musl-x64 \
        /usr/local/bin/agent-browser;                           \
    agent-browser --version

ENV PYTHONUNBUFFERED=1
ENV PYTHONDONTWRITEBYTECODE=1
# браузер для agent-browser: системный chromium вместо загрузки с cdn.
# при WITH_CHROMIUM=0 переменную нужно переопределить своим путём
ENV AGENT_BROWSER_EXECUTABLE_PATH=/usr/bin/chromium
ENV HERMES_HOME=/opt/data
ENV HERMES_WRITE_SAFE_ROOT=/opt/data
ENV HERMES_WEB_DIST=${HERMES_PATH}/hermes_cli/web_dist
ENV HERMES_TUI_DIR=${HERMES_PATH}/ui-tui
ENV HERMES_DISABLE_LAZY_INSTALLS=1
ENV HERMES_LAZY_INSTALL_TARGET=/opt/data/lazy-packages
ENV PLAYWRIGHT_BROWSERS_PATH=${HERMES_PATH}/.playwright
ENV NODE_EXTRA_CA_CERTS=/etc/ssl/certs/ca-certificates.crt
# /opt/hermes/bin перед venv: shim должен выигрывать разрешение по PATH
ENV PATH="/opt/hermes/bin:/opt/hermes/.venv/bin:/opt/data/.local/bin:${PATH}"

RUN mkdir -p /opt/data
VOLUME [ "/opt/data" ]

ENTRYPOINT [ "/init", "/opt/hermes/docker/main-wrapper.sh" ]
CMD [ ]
