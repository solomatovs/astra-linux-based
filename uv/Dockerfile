# uv
ARG RUNTIME_IMAGE=dmp/glibc:2.28
ARG BUILD_IMAGE=dmp/rust:1.90.0

FROM ${RUNTIME_IMAGE} AS base
FROM ${BUILD_IMAGE} AS builder

ARG UV_VERSION
ARG APT_DEPS
ARG UV_PATH=/opt/uv

# по умолчанию пусто: cc/make/perl уже есть в dmp/rust, а нативный код
# (jemalloc, ring) вендорит свои исходники
RUN if [ -n "${APT_DEPS}" ]; then                                       \
        apt-get update                                                  \
        && apt-get install -y --no-install-recommends ${APT_DEPS}       \
        && apt-get clean                                                \
        && rm -rf /var/lib/apt/lists/* /var/cache/apt/archives/*;       \
    fi

# rustc первого звена цепочки dmp/rust собран через mrustc и печатает
# "rustc 1.90.0-stable-mrustc": суффикс стоит на месте канала и ломает build.rs
# у rustversion, version_check и прочих читателей этой строки. шим правит только
# однострочный --version, остальное проксирует как есть (`rustc -vV` корректен,
# cargo им и пользуется). для bootstrap-собранных rustc (1.91+) шим — симлинк
RUN mkdir -p /opt/rustc-shim                                                \
    && if /usr/local/bin/rustc --version | grep -q -- '-stable-mrustc'; then \
        printf '%s\n'                                                       \
            '#!/bin/sh'                                                     \
            'if [ "$#" -eq 1 ] && [ "$1" = "--version" ]; then'             \
            "    /usr/local/bin/rustc --version | sed 's/-stable-mrustc//'" \
            '    exit $?'                                                   \
            'fi'                                                            \
            'exec /usr/local/bin/rustc "$@"'                                \
            > /opt/rustc-shim/rustc                                         \
        && chmod +x /opt/rustc-shim/rustc;                                  \
    else                                                                    \
        ln -s /usr/local/bin/rustc /opt/rustc-shim/rustc;                   \
    fi                                                                      \
    && /opt/rustc-shim/rustc --version                                      \
    && /opt/rustc-shim/rustc -vV                                            \
    && [ "$(/opt/rustc-shim/rustc --version | wc -l)" -eq 1 ]               \
    && [ -z "$(/opt/rustc-shim/rustc --version | grep mrustc || true)" ]

# сеть в сборке не нужна: крейты лежат в vendor/ внутри тарбола
ENV CARGO_NET_OFFLINE=true \
    CC=/usr/local/bin/gcc \
    CXX=/usr/local/bin/g++ \
    RUSTC=/opt/rustc-shim/rustc \
    PATH=/opt/rustc-shim:/usr/local/bin:/usr/bin:/bin:/usr/local/sbin:/usr/sbin:/sbin

# uv
COPY artifacts/src/ /tmp/src/
# профиль release у uv объявлен с lto = "fat", а std первого звена цепочки
# dmp/rust собран без LLVM-биткода в rlib — линковка падает на
# "Can't find section .llvmbc". смотрим биткод и гасим lto только там, где надо
RUN set -eux;                                                           \
    std="$(ls /usr/local/lib/rustlib/x86_64-unknown-linux-gnu/lib/libstd*.rlib | head -1)"; \
    mkdir -p /tmp/bc;                                                   \
    (cd /tmp/bc && ar x "$std" && objdump -h ./*.o | grep -q llvmbc)     \
        && echo "std с биткодом: lto как в профиле uv"                  \
        || { echo "std без биткода: lto выключаем";                     \
             export CARGO_PROFILE_RELEASE_LTO=off; };                   \
    rm -rf /tmp/bc;                                                     \
    cd /tmp;                                                            \
    tar -xf src/uv-${UV_VERSION}.tar.gz;                                \
    cd uv-${UV_VERSION};                                                \
    cargo build --release --offline --locked                            \
        --package uv --bin uv --bin uvx;                                \
    mkdir -p ${UV_PATH}/usr/local/bin ${UV_PATH}/usr/local/share/uv;    \
    cp -a target/release/uv target/release/uvx ${UV_PATH}/usr/local/bin/; \
    cp -a LICENSE-APACHE LICENSE-MIT ${UV_PATH}/usr/local/share/uv/;     \
    cd /; rm -rf /tmp/*

# минимальный glibc, на котором пойдут бинари. считаем здесь, в рантайме нет binutils
RUN set -eux;                                                                       \
    : > ${UV_PATH}/usr/local/share/uv/glibc-floor.txt;                              \
    for b in ${UV_PATH}/usr/local/bin/uv ${UV_PATH}/usr/local/bin/uvx; do           \
        max="$(objdump -T "$b" | grep -o 'GLIBC_[0-9.]*' | sort -Vu | tail -1)";    \
        echo "$(basename "$b") $max" >> ${UV_PATH}/usr/local/share/uv/glibc-floor.txt; \
    done;                                                                           \
    cat ${UV_PATH}/usr/local/share/uv/glibc-floor.txt;                              \
    du -h --max-depth=1 ${UV_PATH}


# dist: только собранный /opt/uv
FROM base AS dist
ARG UV_PATH=/opt/uv
COPY --from=builder ${UV_PATH} ${UV_PATH}


FROM base AS runtime

ARG UV_PATH=/opt/uv
ARG UV_VERSION

# устанавливаем собранный uv в runtime-образ
COPY --from=dist ${UV_PATH} /

# corp nexus
COPY artifacts/uv-config.toml* /tmp/uvconf/
RUN if [ -f /tmp/uvconf/uv-config.toml ]; then          \
        mkdir -p /etc/uv                                \
        && cp /tmp/uvconf/uv-config.toml /etc/uv/uv.toml; \
    fi                                                  \
    && rm -rf /tmp/uvconf

ENV PATH=/usr/local/bin:${PATH}

ENTRYPOINT [ "/usr/local/bin/uv" ]
