# nodejs
ARG BUILD_IMAGE=dmp/glibc:2.28
ARG RUNTIME_IMAGE=dmp/glibc:2.28
ARG PYTHON_IMAGE=dmp/python:3.10-dist
ARG GCC_IMAGE=dmp/gcc:12.4.0-dist
ARG LLVM_IMAGE=dmp/llvm:20.1.8-dist

FROM ${PYTHON_IMAGE}    AS python
FROM ${GCC_IMAGE}       AS gcc
FROM ${LLVM_IMAGE}      AS llvm
FROM ${BUILD_IMAGE}      AS base
FROM base AS builder

ARG NODE_VERSION
ARG PNPM_VERSION
# NODE_CC: clang | gcc — старый V8 (node <=16) не собирается clang 20, ему нужен gcc
ARG NODE_CC=clang
# NODE_PY2: 0 | 1 — node 8/10/12 требуют python2 для configure/gyp
ARG NODE_PY2=0
ARG NODE_PATH=/opt/nodejs
ARG GCC_PATH=/opt/gcc
ARG LLVM_PATH=/opt/llvm

COPY --from=gcc ${GCC_PATH} /
COPY --from=gcc /usr/include/ /usr/include/
COPY --from=llvm ${LLVM_PATH}/usr/local/ /usr/local/
COPY --from=python /opt/python/usr/local/ /usr/local/

RUN ldconfig

COPY artifacts/src/node-v${NODE_VERSION}.tar.xz /tmp/node.tar.xz
RUN set -eux; \
    cd /tmp; \
    tar -xf node.tar.xz; \
    cd node-v${NODE_VERSION}; \
    # выбор компилятора: clang (node 18+) или gcc (старый V8)
    if [ "${NODE_CC}" = "gcc" ]; then \
        export CC=gcc CXX=g++ CC_host=gcc CXX_host=g++; \
    else \
        export CC=clang CXX=clang++ CC_host=clang CXX_host=clang++; \
    fi; \
    # python2 для configure/gyp у старых node; иначе собираем python3
    if [ "${NODE_PY2}" = "1" ]; then \
        apt-get update; \
        apt-get install -y --no-install-recommends python2.7 || apt-get install -y --no-install-recommends python2; \
        apt-get clean; rm -rf /var/lib/apt/lists/* /var/cache/apt/archives/*; \
        CONF_PY="$(command -v python2 || command -v python2.7)"; \
        export PYTHON="$CONF_PY"; \
    else \
        CONF_PY=python3; \
    fi; \
    "$CONF_PY" ./configure --prefix=/usr/local; \
    make -j"$(nproc)"; \
    make install DESTDIR=${NODE_PATH}; \
    rm -f ${NODE_PATH}/usr/local/CHANGELOG.md \
          ${NODE_PATH}/usr/local/LICENSE \
          ${NODE_PATH}/usr/local/README.md; \
    cd /; \
    rm -rf /tmp/*

# pnpm ставится в тот же DESTDIR
COPY artifacts/src/pnpm-${PNPM_VERSION}.tgz /tmp/pnpm.tgz
RUN set -eu; \
    export PATH="${NODE_PATH}/usr/local/bin:${PATH}"; \
    BIN="${NODE_PATH}/usr/local/bin"; \
    NM="${NODE_PATH}/usr/local/lib/node_modules"; \
    npm install -g /tmp/pnpm.tgz --prefix ${NODE_PATH}/usr/local --offline --no-audit --no-fund; \
    ln -sf ../lib/node_modules/npm/bin/npm-cli.js "$BIN/npm"; \
    ln -sf ../lib/node_modules/npm/bin/npx-cli.js "$BIN/npx"; \
    if [ -d "$NM/corepack" ]; then \
        ln -sf ../lib/node_modules/corepack/dist/corepack.js "$BIN/corepack"; \
    fi; \
    pnpm_bin="$(node -p "require('$NM/pnpm/package.json').bin.pnpm")"; \
    pnpx_bin="$(node -p "require('$NM/pnpm/package.json').bin.pnpx")"; \
    ln -sf "../lib/node_modules/pnpm/$pnpm_bin" "$BIN/pnpm"; \
    ln -sf "../lib/node_modules/pnpm/$pnpx_bin" "$BIN/pnpx"; \
    rm -rf /tmp/*


# dist: только скомпилированный /opt/nodejs без тулчейна — компактный «дистрибутив»
FROM base AS dist
ARG NODE_PATH=/opt/nodejs
COPY --from=builder ${NODE_PATH} ${NODE_PATH}


FROM ${RUNTIME_IMAGE} AS runtime

ARG NODE_PATH=/opt/nodejs
ARG GCC_PATH=/opt/gcc

COPY --from=dist ${NODE_PATH} /

COPY --from=gcc ${GCC_PATH}/usr/local/lib64/libstdc++.so.6* ${GCC_PATH}/usr/local/lib64/libgcc_s.so.1* ${GCC_PATH}/usr/local/lib64/libatomic.so.1* /usr/local/lib64/
RUN printf '%s\n'           \
        /usr/local/lib64    \
        /usr/local/lib      \
    > /etc/ld.so.conf.d/nodejs-local.conf \
    && ldconfig

COPY artifacts/npmrc* /tmp/npmrc/
RUN if [ -f /tmp/npmrc/npmrc ]; then \
        cp /tmp/npmrc/npmrc /usr/local/etc/npmrc; \
    fi \
    && rm -rf /tmp/npmrc

ENV NODE_EXTRA_CA_CERTS=/etc/ssl/certs/ca-certificates.crt

ENTRYPOINT [ "/usr/local/bin/node" ]
