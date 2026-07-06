# rust
ARG BUILD_IMAGE=dmp/gcc:11.5.0
ARG RUNTIME_IMAGE=dmp/gcc:11.5.0
ARG CMAKE_IMAGE=dmp/cmake:3.27.9-dist
ARG PYTHON_IMAGE=dmp/python:3.11-dist

FROM ${CMAKE_IMAGE} AS cmake
FROM ${PYTHON_IMAGE} AS python
FROM ${BUILD_IMAGE} AS dist

ARG MRUSTC_VERSION
ARG RUST_VERSION
ARG APT_DEPS
ARG RUST_PATH=/opt/rust

# cmake, python from another image
COPY --from=cmake  /opt/cmake  /
COPY --from=python /opt/python /
RUN ln -sf /usr/local/bin/python3.11 /usr/local/bin/python3 && ldconfig
RUN apt-get update && apt-get install -y --no-install-recommends ${APT_DEPS} \
    && rm -rf /var/lib/apt/lists/*

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
    ./output-${RUST_VERSION}/cargo --version

# save all output
RUN set -eux; \
    mkdir -p ${RUST_PATH}/opt; \
    cp -a /build/mrustc-${MRUSTC_VERSION}/output-${RUST_VERSION} ${RUST_PATH}/opt/rust-output; \
    du -sh ${RUST_PATH}/opt/rust-output; \
    ls -la ${RUST_PATH}/opt/rust-output | head -40


FROM ${RUNTIME_IMAGE} AS runtime

ARG RUST_PATH=/opt/rust

COPY --from=dist ${RUST_PATH}/opt/rust-output /opt/rust-output

# ВРЕМЕННО: раскладка sysroot будет уточнена после инспекции dist-образа.
RUN ln -sf /opt/rust-output/rustc /usr/local/bin/rustc \
    && ln -sf /opt/rust-output/cargo /usr/local/bin/cargo \
    && ldconfig || true

# CA
COPY artifacts/ca-chain.crt* /tmp/cacrt/
RUN if [ -f /tmp/cacrt/ca-chain.crt ] && grep -qE '^-----BEGIN CERTIFICATE-----' /tmp/cacrt/ca-chain.crt; then \
        cp /tmp/cacrt/ca-chain.crt /usr/local/share/ca-certificates/ca-chain.crt \
        && update-ca-certificates; \
    fi \
    && rm -rf /tmp/cacrt

# nexus mirror
COPY artifacts/cargo-config.toml* /tmp/cargoconf/
RUN if [ -f /tmp/cargoconf/cargo-config.toml ]; then \
        mkdir -p /usr/local/cargo && cp /tmp/cargoconf/cargo-config.toml /usr/local/cargo/config.toml; \
    fi \
    && rm -rf /tmp/cargoconf

ENV CARGO_HOME=/usr/local/cargo \
    PATH=/opt/rust-output:/usr/local/bin:${PATH}

ENTRYPOINT [ "/usr/local/bin/rustc" ]
