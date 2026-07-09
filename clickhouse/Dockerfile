# clickhouse
ARG BUILD_IMAGE=dmp/glibc:2.28
ARG RUNTIME_IMAGE=dmp/glibc:2.28
ARG CMAKE_IMAGE=dmp/cmake:3.27.9-dist
ARG PYTHON_IMAGE=dmp/python:3.10-dist
ARG GCC_IMAGE=dmp/gcc:12.4.0-dist
ARG LLVM_IMAGE=dmp/llvm:20.1.8-dist

FROM ${CMAKE_IMAGE} AS cmake
FROM ${PYTHON_IMAGE} AS python
FROM ${GCC_IMAGE} AS gcc
FROM ${LLVM_IMAGE} AS llvm
FROM ${BUILD_IMAGE} AS base
FROM base AS builder

ARG CLICKHOUSE_VERSION
ARG NINJA_VERSION
# CLICKHOUSE_CC: clang | gcc — ClickHouse собирается только clang (gcc официально не поддерживается)
ARG CLICKHOUSE_CC=clang
ARG CLICKHOUSE_PATH=/opt/clickhouse
ARG GCC_PATH=/opt/gcc
ARG LLVM_PATH=/opt/llvm


# cmake
COPY --from=cmake /opt/cmake     /
# python
COPY --from=python /opt/python   /
# gcc
COPY --from=gcc ${GCC_PATH}      /
COPY --from=gcc /usr/include/    /usr/include/
# llvm
COPY --from=llvm ${LLVM_PATH}/usr/local/ /usr/local/

RUN printf '%s\n'           \
        /usr/local/lib64    \
        /usr/local/lib      \
        > /etc/ld.so.conf.d/clickhouse-local.conf \
    && ldconfig

RUN apt-get update                                  \
    && apt-get install -y --no-install-recommends   \
        nasm yasm curl git                          \
    && apt-get clean                                \
    && rm -rf /var/lib/apt/lists/* /var/cache/apt/archives/*

COPY artifacts/src/ /tmp/src/

# ninja
RUN cd /tmp \
    && tar -xf src/ninja-${NINJA_VERSION}.tar.gz \
    && cmake -S ninja-${NINJA_VERSION} -B ninja-build -DCMAKE_BUILD_TYPE=Release \
    && cmake --build ninja-build -j"$(nproc)" \
    && install -m 0755 ninja-build/ninja /usr/local/bin/ninja \
    && rm -rf /tmp/ninja-*

# clickhouse
RUN cd /tmp \
    && tar -xf src/clickhouse-${CLICKHOUSE_VERSION}.tar.gz \
    # git init: кодогенерация лицензий зовёт git rev-parse (в tarball нет .git)
    && git -C ClickHouse-${CLICKHOUSE_VERSION} init -q \
    # выбор компилятора: clang (по умолчанию) или gcc
    && if [ "${CLICKHOUSE_CC}" = "gcc" ]; then export CC=gcc CXX=g++; else export CC=clang CXX=clang++; fi \
    && cmake -G Ninja -S ClickHouse-${CLICKHOUSE_VERSION} -B build \
        -DCMAKE_BUILD_TYPE=Release                      \
        -DCMAKE_C_COMPILER=$CC                          \
        -DCMAKE_CXX_COMPILER=$CXX                       \
        -DENABLE_CLICKHOUSE_ALL=ON                      \
        -DWERROR=OFF                                    \
        -DENABLE_TESTS=OFF                              \
        -DENABLE_RUST=OFF                               \
        -DENABLE_UTILS=OFF                              \
        -DENABLE_EMBEDDED_COMPILER=OFF                  \
        -DENABLE_CCACHE=0                               \
        -DCOMPILER_CACHE=disabled                       \
        -DCMAKE_INSTALL_PREFIX=/usr/local               \
    && cmake --build build -j"$(nproc)"                 \
    && DESTDIR=${CLICKHOUSE_PATH} cmake --install build \
    && cd / \
    && rm -rf /tmp/*


# dist: только собранный /opt/clickhouse без тулчейна — компактный «дистрибутив»
FROM ${RUNTIME_IMAGE} AS dist
ARG CLICKHOUSE_PATH=/opt/clickhouse
COPY --from=builder ${CLICKHOUSE_PATH} ${CLICKHOUSE_PATH}


FROM ${RUNTIME_IMAGE} AS runtime

ARG CLICKHOUSE_PATH=/opt/clickhouse

COPY --from=dist ${CLICKHOUSE_PATH} /
RUN printf '%s\n'           \
        /usr/local/lib      \
        /usr/local/lib64    \
        > /etc/ld.so.conf.d/clickhouse-local.conf \
    && ldconfig

ENTRYPOINT [ "/usr/local/bin/clickhouse" ]
