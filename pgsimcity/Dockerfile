# pgsimcity — PGSimCity: интерактивная 3D-модель PostgreSQL (three.js/vite/typescript).
# Итог сборки — статический бандл (включая wasm-сборку PGlite), раздается nginx'ом
ARG RUNTIME_IMAGE=dmp/nginx:1.28
ARG NODE_IMAGE=dmp/nodejs:22
ARG SITE_PATH=/opt/pgsimcity

# runtime берёт дистрибутив из готового -dist образа (--build-arg DIST_IMAGE),
# иначе стадия builder компилировалась бы заново
ARG DIST_IMAGE=scratch

FROM ${RUNTIME_IMAGE} AS base

FROM ${NODE_IMAGE} AS builder

ARG SITE_VERSION
ARG SITE_PATH

COPY artifacts/src/pgsimcity-${SITE_VERSION}-src.tar.gz /tmp/src.tar.gz
# npm-кэш, свендоренный на этапе make sources: docker-сборка в сеть не ходит,
# поэтому npmrc/Nexus здесь не нужны — см. npmrc.example
COPY artifacts/src/npm-cache-${SITE_VERSION}.tar.gz /tmp/npm-cache.tar.gz
# wasm-сборка rollup той же версии, что в package-lock (качается на make sources)
COPY artifacts/src/rollup-wasm-${SITE_VERSION}.tgz /tmp/rollup-wasm.tgz

RUN set -eux;                                                       \
    mkdir /tmp/build;                                               \
    tar -xzf /tmp/src.tar.gz -C /tmp/build --strip-components=1;    \
    tar -xzf /tmp/npm-cache.tar.gz -C /tmp/build;                   \
    cd /tmp/build;                                                  \
    # аналитика plausible.io — единственный внешний ресурс страницы,
    # в закрытом контуре вырезаем (PGlite бандлится из npm, не с CDN)
    sed -i '/plausible\.io/d' index.html;                           \
    # --offline: все пакеты обязаны быть в кэше, при недостаче сборка
    # падает, а не лезет в сеть
    npm ci --offline --cache .npm-cache --no-audit --no-fund;       \
    # нативному биндингу rollup 4 (его тянет vite) нужен glibc >= 2.29, а базовый
    # dmp/glibc — 2.28, JS-фолбэка у rollup нет. Штатный для vite обходной путь —
    # заменить rollup на его же wasm-сборку @rollup/wasm-node (тот же API и
    # версия, npm делает это алиасом "rollup": "npm:@rollup/wasm-node").
    # Бандл получается побайтово тот же, что с нативным биндингом
    rm -rf node_modules/rollup node_modules/@rollup/rollup-linux-x64-gnu;  \
    mkdir /tmp/rollup-wasm;                                         \
    tar -xzf /tmp/rollup-wasm.tgz -C /tmp/rollup-wasm;              \
    mv /tmp/rollup-wasm/package node_modules/rollup;                \
    npm run build;                                                  \
    test -f dist/index.html;                                        \
    ! grep -r 'plausible\.io' dist/;                                \
    # wasm-сборка PostgreSQL (PGlite) должна попасть в бандл
    test -n "$(find dist -name '*.wasm' -print -quit)";             \
    mkdir -p ${SITE_PATH};                                          \
    cp -a dist/. ${SITE_PATH}/;                                     \
    du -h --max-depth=1 ${SITE_PATH};                               \
    cd /; rm -rf /tmp/build /tmp/src.tar.gz /tmp/npm-cache.tar.gz /tmp/rollup-wasm*


# dist: только собранный статический бандл — без node и node_modules
FROM base AS dist
ARG SITE_PATH
COPY --from=builder ${SITE_PATH} ${SITE_PATH}


FROM ${DIST_IMAGE} AS distimg

FROM base AS runtime
ARG SITE_PATH

RUN rm -rf /etc/nginx/html
COPY --from=distimg ${SITE_PATH} /etc/nginx/html
COPY nginx.conf /etc/nginx/nginx.conf

EXPOSE 80

# curl'а в образе нет — проверка через bash /dev/tcp
HEALTHCHECK --interval=30s --timeout=5s --start-period=5s --retries=3 \
    CMD ["/bin/bash", "-c", "exec 3<>/dev/tcp/127.0.0.1/80; printf 'GET /healthz HTTP/1.0\\r\\n\\r\\n' >&3; head -1 <&3 | grep -q ' 200 '"]

# ENTRYPOINT/CMD наследуются от базового dmp/nginx: nginx -g 'daemon off;'
