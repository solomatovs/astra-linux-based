# rust
ARG RUNTIME_IMAGE=dmp/gcc:8.5.0
ARG BUILD_IMAGE=dmp/gcc:8.5.0
ARG CMAKE_IMAGE=dmp/cmake:3.27.9-dist
ARG PYTHON_IMAGE=dmp/python:3.10-dist
# откуда берётся компилятор для сборки:
#   mrustc    - первый rustc в цепочке, поднимается из C++ (rust-компилятора ещё нет)
#   bootstrap - self-hosting: rustc из BUILD_IMAGE собирает следующую версию через x.py
# ветка выбирается через FROM build-${BUILD_MODE}, неиспользуемая стадия не собирается
ARG BUILD_MODE=mrustc

# runtime берёт дистрибутив из готового -dist образа (--build-arg DIST_IMAGE),
# иначе стадия builder компилировалась бы заново
ARG DIST_IMAGE=scratch

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


# общая часть обеих веток: cmake, python, apt-зависимости и rust из предыдущего образа
FROM base AS toolbox

ARG APT_DEPS

# cmake, python from another image
COPY --from=cmake  /opt/cmake  /
COPY --from=python /opt/python /
RUN ldconfig

RUN apt-get update                                              \
    && apt-get install -y --no-install-recommends ${APT_DEPS}   \
    && rm -rf /var/lib/apt/lists/*

# копируем rust из предыдущего образа если он есть в этом образе
COPY --from=prev /seed/ /
# rustc/cargo линкуют libstd.so, без ldconfig seed-компилятор не запустится
RUN printf '%s\n' \
        /usr/local/lib \
        /usr/local/lib/rustlib/x86_64-unknown-linux-gnu/lib \
        > /etc/ld.so.conf.d/rust-local.conf \
    && ldconfig


# ветка mrustc: rustc собирается из исходников компилятором C++, без бинарного rustc.
# нужна только для первого звена цепочки — дальше дешевле bootstrap (см. ниже)
FROM toolbox AS build-mrustc

ARG MRUSTC_VERSION
ARG RUST_VERSION
ARG RUST_PATH=/opt/rust
# минор rust-версии (1.90.0 -> 1.90): по нему mrustc выбирает набор патчей и
# script-overrides. считается в Makefile — ENV не умеет shell-подстановку
ARG MRUSTC_TARGET_VER
# сколько rustc гонять параллельно на этапе run_rustc. пусто = min(nproc, 8).
# rustc там — тот, что собрал mrustc: неоптимизированный и прожорливый по памяти,
# на машине с малым объёмом RAM на ядро -j nproc роняет его по SIGABRT
# (неудачный malloc; OOM-killer дал бы signal 9)
ARG MRUSTC_JOBS

ENV RUSTC_VERSION=${RUST_VERSION} \
    MRUSTC_TARGET_VER=${MRUSTC_TARGET_VER} \
    OUTDIR_SUF=-${RUST_VERSION}

WORKDIR /build
COPY artifacts/src/mrustc-${MRUSTC_VERSION}.tar.gz /build/
RUN tar xzf mrustc-${MRUSTC_VERSION}.tar.gz && rm mrustc-${MRUSTC_VERSION}.tar.gz

# `make RUSTCSRC` увидит файл и не будет использовать сеть для скачивания
COPY artifacts/src/rustc-${RUST_VERSION}-src.tar.gz /build/mrustc-${MRUSTC_VERSION}/

WORKDIR /build/mrustc-${MRUSTC_VERSION}

# mrustc помечает rustc как "<версия>-stable-mrustc" (ломает build.rs у крейтов,
# читающих rustc --version) и собирает std без LLVM-биткода (ломает lto = "fat")
RUN set -eux; \
    grep -q 'CFG_VERSION=$(RUSTC_VERSION)-stable-mrustc' run_rustc/Makefile; \
    sed -i 's/CFG_VERSION=$(RUSTC_VERSION)-stable-mrustc/CFG_VERSION=$(RUSTC_VERSION)/' run_rustc/Makefile; \
    grep -q 'RUSTFLAGS="-Z force-unstable-if-unmarked' run_rustc/Makefile; \
    sed -i 's/RUSTFLAGS="-Z force-unstable-if-unmarked/RUSTFLAGS="-C embed-bitcode=yes -Z force-unstable-if-unmarked/' run_rustc/Makefile; \
    grep -n 'CFG_VERSION=\|RUSTFLAGS=' run_rustc/Makefile

# взято из build-<ver>.sh, но разложено по слоям: этап целиком — это часы (местами
# сутки), и одним RUN любое падение на последнем шаге стирало бы всю работу
RUN set -eux; \
    test "${MRUSTC_TARGET_VER}" = "$(echo ${RUST_VERSION} | cut -d. -f1-2)"; \
    make -j"$(nproc)"; \
    make RUSTCSRC

# std и зависимости rustc — компилирует сам mrustc (C++), без rustc
RUN set -eux; \
    make -f minicargo.mk -j"$(nproc)" LIBS

# rustc и cargo. работают, но собраны mrustc — то есть без оптимизаций и медленные
RUN set -eux; \
    RUSTC_INSTALL_BINDIR=bin make -f minicargo.mk -j"$(nproc)" output-${RUST_VERSION}/rustc; \
    make -f minicargo.mk -j"$(nproc)" output-${RUST_VERSION}/cargo; \
    ./output-${RUST_VERSION}/rustc --version; \
    ./output-${RUST_VERSION}/cargo --version

# run_rustc: std и rustc пересобираются уже этим rustc-ом, чтобы совпали ABI.
# minicargo подхватывает jobserver внешнего make, поэтому параллелизм задаётся
# здесь. настоящее сообщение об ошибке rustc уходит в <крейт>.rlib_dbg.txt —
# на падении показываем хвосты, иначе в логе остаётся только "signal 6"
RUN set -eux; \
    JOBS="${MRUSTC_JOBS:-}"; \
    if [ -z "$JOBS" ]; then JOBS="$(nproc)"; [ "$JOBS" -le 8 ] || JOBS=8; fi; \
    make -C run_rustc -j"$JOBS" RUSTC_VERSION=${RUST_VERSION} || { \
        echo "=== run_rustc упал, хвосты последних rustc-логов ==="; \
        ls -t run_rustc/output-${RUST_VERSION}/prefix*/lib/rustlib/*/lib/*_dbg.txt 2>/dev/null \
            | head -5 \
            | while read -r f; do echo "--- $f"; tail -n 40 "$f"; done; \
        exit 1; \
    }

RUN set -eux; \
    PFX="$(pwd)/run_rustc/output-${RUST_VERSION}/prefix"; \
    $PFX/bin/rustc --version; \
    test "$($PFX/bin/rustc --version)" = "rustc ${RUST_VERSION}"; \
    mkdir /tmp/bc && cd /tmp/bc \
        && ar x $PFX/lib/rustlib/x86_64-unknown-linux-gnu/lib/libstd.rlib \
        && objdump -h *.o | grep -q llvmbc; \
    cd /; rm -rf /tmp/bc

# install toolchain (rustc+cargo+std sysroot)
RUN set -eux; \
    PFX=/build/mrustc-${MRUSTC_VERSION}/run_rustc/output-${RUST_VERSION}/prefix; \
    mkdir -p ${RUST_PATH}/usr/local; \
    cp -a "$PFX/." ${RUST_PATH}/usr/local/; \
    du -sh ${RUST_PATH}/usr/local; \
    ls -la ${RUST_PATH}/usr/local/bin


# ветка bootstrap: штатная сборка rustc самим rustc (stage0 - компилятор из toolbox).
# так собирается каждое звено цепочки, кроме первого: часы вместо суток у mrustc
FROM toolbox AS build-bootstrap

ARG RUST_VERSION
ARG NINJA_VERSION
ARG RUST_PATH=/opt/rust

ENV CC=/usr/local/bin/gcc \
    CXX=/usr/local/bin/g++

# stage0: rustc/cargo предыдущей версии, приехали из prev в /usr/local.
# seed от mrustc печатает "rustc <версия>-stable-mrustc" — суффикс стоит на месте
# канала и ломает build.rs у rustversion/version_check в vendor/ исходников rustc.
# шим правит только однострочный --version, остальное проксирует как есть
RUN set -eux; \
    mkdir -p /opt/stage0/bin; \
    ln -s /usr/local/bin/cargo /opt/stage0/bin/cargo; \
    if rustc --version | grep -q -- '-stable-mrustc'; then \
        printf '%s\n'                                                   \
            '#!/bin/sh'                                                 \
            'if [ "$#" -eq 1 ] && [ "$1" = "--version" ]; then'         \
            "    /usr/local/bin/rustc --version | sed 's/-stable-mrustc//'" \
            '    exit $?'                                               \
            'fi'                                                        \
            'exec /usr/local/bin/rustc "$@"'                            \
            > /opt/stage0/bin/rustc;                                    \
        chmod +x /opt/stage0/bin/rustc;                                 \
    else                                                                \
        ln -s /usr/local/bin/rustc /opt/stage0/bin/rustc;               \
    fi; \
    /opt/stage0/bin/rustc --version; \
    /opt/stage0/bin/cargo --version; \
    [ "$(/opt/stage0/bin/rustc --version | wc -l)" -eq 1 ]; \
    [ -z "$(/opt/stage0/bin/rustc --version | grep mrustc || true)" ]

# ninja: llvm из состава исходников rustc собирается через cmake -G Ninja
WORKDIR /build
COPY artifacts/src/ninja-${NINJA_VERSION}.tar.gz /build/
RUN set -eux; \
    tar -xf ninja-${NINJA_VERSION}.tar.gz; \
    cmake -S ninja-${NINJA_VERSION} -B ninja-build -DCMAKE_BUILD_TYPE=Release; \
    cmake --build ninja-build -j"$(nproc)"; \
    install -m 0755 ninja-build/ninja /usr/local/bin/ninja; \
    rm -rf ninja-${NINJA_VERSION} ninja-build ninja-${NINJA_VERSION}.tar.gz; \
    ninja --version

COPY artifacts/src/rustc-${RUST_VERSION}-src.tar.gz /build/
RUN set -eux; \
    tar -xf rustc-${RUST_VERSION}-src.tar.gz; \
    rm rustc-${RUST_VERSION}-src.tar.gz

WORKDIR /build/rustc-${RUST_VERSION}-src

# сеть не нужна: крейты лежат в vendor/ внутри тарбола, build.vendor включится сам.
# llvm берём из состава исходников (src/llvm-project), только под X86 — иначе
# сборка LLVM под все два десятка платформ занимает больше, чем сам rustc.
# install.prefix задан так, чтобы разложить тулчейн сразу в ${RUST_PATH}/usr/local,
# как это делает ветка mrustc: rustc ищет sysroot относительно своего бинаря
RUN set -eux; \
    { \
        echo 'change-id = "ignore"'; \
        echo '[build]'; \
        echo 'rustc = "/opt/stage0/bin/rustc"'; \
        echo 'cargo = "/opt/stage0/bin/cargo"'; \
        echo 'target = ["x86_64-unknown-linux-gnu"]'; \
        echo 'extended = true'; \
        echo 'tools = ["cargo"]'; \
        echo 'docs = false'; \
        echo 'verbose = 0'; \
        echo '[llvm]'; \
        echo 'download-ci-llvm = false'; \
        echo 'ninja = true'; \
        echo 'targets = "X86"'; \
        echo 'assertions = false'; \
        echo '[rust]'; \
        echo 'channel = "stable"'; \
        echo 'omit-git-hash = true'; \
        echo 'llvm-tools = false'; \
        echo '[target.x86_64-unknown-linux-gnu]'; \
        echo 'cc = "/usr/local/bin/gcc"'; \
        echo 'cxx = "/usr/local/bin/g++"'; \
        echo 'ar = "/usr/local/bin/ar"'; \
        echo 'ranlib = "/usr/local/bin/ranlib"'; \
        echo '[install]'; \
        echo "prefix = \"${RUST_PATH}/usr/local\""; \
        echo 'sysconfdir = "etc"'; \
    } > bootstrap.toml; \
    cat bootstrap.toml

RUN set -eux; \
    python3 x.py install -j"$(nproc)"; \
    rm -rf /build/rustc-${RUST_VERSION}-src

# те же проверки, что и в ветке mrustc: версия без посторонних суффиксов и
# LLVM-биткод в std (без него у зависимых проектов падает lto = "fat")
RUN set -eux; \
    PFX=${RUST_PATH}/usr/local; \
    LD_LIBRARY_PATH=$PFX/lib $PFX/bin/rustc --version; \
    LD_LIBRARY_PATH=$PFX/lib $PFX/bin/rustc -vV; \
    test "$(LD_LIBRARY_PATH=$PFX/lib $PFX/bin/rustc --version | cut -d' ' -f1-2)" = "rustc ${RUST_VERSION}"; \
    mkdir /tmp/bc && cd /tmp/bc \
        && ar x "$(ls $PFX/lib/rustlib/x86_64-unknown-linux-gnu/lib/libstd-*.rlib | head -1)" \
        && objdump -h *.o | grep -q llvmbc; \
    cd /; rm -rf /tmp/bc; \
    du -sh ${RUST_PATH}/usr/local; \
    ls -la ${RUST_PATH}/usr/local/bin


FROM build-${BUILD_MODE} AS builder


# dist: только установленный тулчейн /opt/rust/usr/local без сборочного дерева — компактный «дистрибутив»
FROM base AS dist
ARG RUST_PATH=/opt/rust
COPY --from=builder ${RUST_PATH}/usr/local ${RUST_PATH}/usr/local


FROM ${DIST_IMAGE} AS distimg

FROM base AS runtime

ARG RUST_PATH=/opt/rust

COPY --from=distimg ${RUST_PATH}/usr/local /usr/local

# rustc/cargo (rust-бинари) линкуют libstd.so
# кладём rust-lib-каталоги в ldconfig
RUN printf '%s\n' \
        /usr/local/lib \
        /usr/local/lib/rustlib/x86_64-unknown-linux-gnu/lib \
        > /etc/ld.so.conf.d/rust-local.conf \
    && ldconfig

# corp nexus
COPY cargo-config.toml* /tmp/cargoconf/
RUN if [ -f /tmp/cargoconf/cargo-config.toml ]; then \
        mkdir -p /usr/local/cargo && cp /tmp/cargoconf/cargo-config.toml /usr/local/cargo/config.toml; \
    fi \
    && rm -rf /tmp/cargoconf

ENV CARGO_HOME=/usr/local/cargo \
    PATH=/usr/local/bin:${PATH}

ENTRYPOINT [ "/usr/local/bin/rustc" ]
