# rust
ARG RUNTIME_IMAGE=dmp/gcc:8.5.0
ARG BUILD_IMAGE=dmp/gcc:8.5.0
ARG CMAKE_IMAGE=dmp/cmake:3.27.9-dist
ARG PYTHON_IMAGE=dmp/python:3.10-dist

FROM ${BUILD_IMAGE} AS prev
ARG RUST_PATH=/opt/rust

# надо проверить prev образ, существует ли там новый скомпилированный rust /opt/rust
# если он есть, то копируем rust в папку /seed
# так как BUILD_IMAGE может быть двух вариантов:
# 1. базовый gcc образ
# 2. предыдущий rust образ
RUN mkdir -p /seed \
    && if [ -d "${RUST_PATH}" ] && [ -n "$(ls -A "${RUST_PATH}" 2>/dev/null)" ]; then \
        cp -a "${RUST_PATH}/." /seed/; \
    fi

FROM ${CMAKE_IMAGE} AS cmake
FROM ${PYTHON_IMAGE} AS python

FROM ${RUNTIME_IMAGE} AS base
FROM base AS builder

ARG MRUSTC_VERSION
ARG RUST_VERSION
ARG APT_DEPS
ARG RUST_PATH=/opt/rust

# cmake, python from another image
COPY --from=cmake  /opt/cmake  /
COPY --from=python /opt/python /
RUN ldconfig

RUN apt-get update                                              \
    && apt-get install -y --no-install-recommends ${APT_DEPS}   \
    && rm -rf /var/lib/apt/lists/*

# копируем rust из предыдущего образа если он есть в этом образе
COPY --from=prev /seed/ /

WORKDIR /build
COPY artifacts/src/mrustc-${MRUSTC_VERSION}.tar.gz /build/
RUN tar xzf mrustc-${MRUSTC_VERSION}.tar.gz && rm mrustc-${MRUSTC_VERSION}.tar.gz

# `make RUSTCSRC` увидит файл и не будет использовать сеть для скачивания
COPY artifacts/src/rustc-${RUST_VERSION}-src.tar.gz /build/mrustc-${MRUSTC_VERSION}/

WORKDIR /build/mrustc-${MRUSTC_VERSION}
# взято из из build-<ver>.sh
RUN set -eux; \
    MINOR="$(echo ${RUST_VERSION} | cut -d. -f1-2)"; \
    export RUSTC_VERSION=${RUST_VERSION} MRUSTC_TARGET_VER="$MINOR" OUTDIR_SUF=-${RUST_VERSION}; \
    make -j"$(nproc)"; \
    make RUSTCSRC; \
    make -f minicargo.mk -j"$(nproc)" LIBS; \
    RUSTC_INSTALL_BINDIR=bin make -f minicargo.mk -j"$(nproc)" output-${RUST_VERSION}/rustc; \
    make -f minicargo.mk -j"$(nproc)" output-${RUST_VERSION}/cargo; \
    ./output-${RUST_VERSION}/rustc --version; \
    ./output-${RUST_VERSION}/cargo --version; \
    make -C run_rustc -j"$(nproc)" RUSTC_VERSION=${RUST_VERSION}; \
    run_rustc/output-${RUST_VERSION}/prefix/bin/rustc --version

# install toolchain (rustc+cargo+std sysroot)
RUN set -eux; \
    PFX=/build/mrustc-${MRUSTC_VERSION}/run_rustc/output-${RUST_VERSION}/prefix; \
    mkdir -p ${RUST_PATH}/usr/local; \
    cp -a "$PFX/." ${RUST_PATH}/usr/local/; \
    du -sh ${RUST_PATH}/usr/local; \
    ls -la ${RUST_PATH}/usr/local/bin


# dist: только установленный тулчейн /opt/rust/usr/local без сборочного дерева — компактный «дистрибутив»
FROM base AS dist
ARG RUST_PATH=/opt/rust
COPY --from=builder ${RUST_PATH}/usr/local ${RUST_PATH}/usr/local


FROM base AS runtime

ARG RUST_PATH=/opt/rust

COPY --from=dist ${RUST_PATH}/usr/local /usr/local

# rustc/cargo (rust-бинари) линкуют libstd.so
# кладём rust-lib-каталоги в ldconfig
RUN printf '%s\n' \
        /usr/local/lib \
        /usr/local/lib/rustlib/x86_64-unknown-linux-gnu/lib \
        > /etc/ld.so.conf.d/rust-local.conf \
    && ldconfig

# corp ca
COPY artifacts/ca-chain.crt* /tmp/cacrt/
RUN if [ -f /tmp/cacrt/ca-chain.crt ] && grep -qE '^-----BEGIN CERTIFICATE-----' /tmp/cacrt/ca-chain.crt; then \
        cp /tmp/cacrt/ca-chain.crt /usr/local/share/ca-certificates/ca-chain.crt \
        && update-ca-certificates; \
    fi \
    && rm -rf /tmp/cacrt

# corp nexus
COPY artifacts/cargo-config.toml* /tmp/cargoconf/
RUN if [ -f /tmp/cargoconf/cargo-config.toml ]; then \
        mkdir -p /usr/local/cargo && cp /tmp/cargoconf/cargo-config.toml /usr/local/cargo/config.toml; \
    fi \
    && rm -rf /tmp/cargoconf

ENV CARGO_HOME=/usr/local/cargo \
    PATH=/usr/local/bin:${PATH}

ENTRYPOINT [ "/usr/local/bin/rustc" ]
