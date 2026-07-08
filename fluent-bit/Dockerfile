# fluent-bit
ARG BUILD_IMAGE=dmp/gcc:14.2.0-dist
ARG RUNTIME_IMAGE=dmp/glibc:2.28
ARG CMAKE_IMAGE=dmp/cmake:3.27.9-dist

FROM ${CMAKE_IMAGE} AS cmake
FROM ${BUILD_IMAGE} AS dist

ARG FLB_VERSION
ARG OPENSSL_VERSION
ARG LIBYAML_VERSION
ARG APT_DEPS
ARG FLB_PATH=/opt/fluent-bit
ARG DEPS_PREFIX=/opt/deps


# cmake из образа dmp/cmake
COPY --from=cmake /opt/cmake /

# gcc-14 toolchain в -dist образе лежит в /opt/gcc — переносим в /usr/local
RUN cp -a /opt/gcc/usr/local/. /usr/local/                                \
    && printf '%s\n'                                                      \
        /usr/local/lib64                                                  \
        /usr/local/lib                                                    \
        > /etc/ld.so.conf.d/gcc-local.conf                                \
    && ldconfig

# build-toolchain (flex/bison/m4 — парсеры fluent-bit, pkg-config — поиск зависимостей)
RUN if [ -n "${APT_DEPS}" ]; then                                         \
        apt-get update                                                    \
        && apt-get install -y --no-install-recommends ${APT_DEPS}         \
        && apt-get clean                                                  \
        && rm -rf /var/lib/apt/lists/* /var/cache/apt/archives/*;         \
    fi

ENV CC=/usr/local/bin/gcc CXX=/usr/local/bin/g++

COPY artifacts/src/ /tmp/src/

# openssl (статически, no-shared)
RUN set -eux;                                                             \
    cd /tmp;                                                              \
    tar -xf src/openssl-${OPENSSL_VERSION}.tar.gz;                        \
    cd openssl-${OPENSSL_VERSION};                                        \
    ./Configure linux-x86_64 no-shared no-tests                           \
        --prefix=${DEPS_PREFIX} --openssldir=${DEPS_PREFIX}/ssl;          \
    make -j"$(nproc)";                                                    \
    make install_sw;                                                      \
    cd /; rm -rf /tmp/openssl-${OPENSSL_VERSION}

# libyaml (статически) — fluent-bit его не вендорит, нужен для YAML-конфигов
RUN set -eux;                                                             \
    cd /tmp;                                                              \
    tar -xf src/yaml-${LIBYAML_VERSION}.tar.gz;                           \
    cd yaml-${LIBYAML_VERSION};                                           \
    ./configure --prefix=${DEPS_PREFIX} --disable-shared --enable-static; \
    make -j"$(nproc)";                                                    \
    make install;                                                         \
    cd /; rm -rf /tmp/yaml-${LIBYAML_VERSION}

# fluent-bit: FLB_ALL — максимум плагинов, openssl/libyaml/libstdc++ линкуются статически
RUN set -eux;                                                           \
    cd /tmp;                                                            \
    tar -xf src/fluent-bit-${FLB_VERSION}.tar.gz;                       \
    cmake -S fluent-bit-${FLB_VERSION} -B build                         \
        -DCMAKE_C_COMPILER=/usr/local/bin/gcc                           \
        -DCMAKE_CXX_COMPILER=/usr/local/bin/g++                         \
        -DCMAKE_C_FLAGS="-D_GNU_SOURCE"                                 \
        -DCMAKE_INSTALL_PREFIX=/usr/local                               \
        -DFLB_RELEASE=On                                                \
        -DFLB_ALL=On                                                    \
        -DFLB_JEMALLOC=On                                               \
        -DFLB_TLS=On                                                    \
        -DFLB_KAFKA=On                                                  \
        -DFLB_HTTP_SERVER=On                                            \
        -DFLB_LUAJIT=On                                                 \
        -DFLB_WASM=On                                                   \
        -DFLB_STREAM_PROCESSOR=On                                       \
        -DFLB_RECORD_ACCESSOR=On                                        \
        -DFLB_SIGNV4=On                                                 \
        -DFLB_METRICS=On                                                \
        -DFLB_CONFIG_YAML=On                                            \
        -DFLB_SHARED_LIB=Off                                            \
        -DFLB_EXAMPLES=Off                                              \
        -DFLB_TESTS_INTERNAL=Off                                        \
        -DFLB_TESTS_RUNTIME=Off                                         \
        -DOPENSSL_ROOT_DIR=${DEPS_PREFIX}                               \
        -DOPENSSL_USE_STATIC_LIBS=On                                    \
        -DFLB_LIBYAML_DIR=${DEPS_PREFIX}                                \
        -DCMAKE_EXE_LINKER_FLAGS="-static-libgcc -static-libstdc++";    \
    cmake --build build -j"$(nproc)";                                   \
    DESTDIR=${FLB_PATH} cmake --install build;                          \
    strip ${FLB_PATH}/usr/local/bin/fluent-bit || true;                 \
    du -h --max-depth=2 ${FLB_PATH};                                    \
    cd /; rm -rf /tmp/*


FROM ${RUNTIME_IMAGE} AS runtime

ARG FLB_PATH=/opt/fluent-bit

# устанавливаем собранный fluent-bit поверх чистого dmp/glibc
COPY --from=dist ${FLB_PATH} /
RUN printf '%s\n'           \
        /usr/local/lib      \
        /usr/local/lib64    \
        > /etc/ld.so.conf.d/fluent-bit-local.conf \
    && ldconfig

# http-сервер мониторинга — 2020, forward — 24224
EXPOSE 2020 24224
STOPSIGNAL SIGTERM

ENTRYPOINT [ "/usr/local/bin/fluent-bit" ]
CMD [ "-c", "/usr/local/etc/fluent-bit/fluent-bit.conf" ]
