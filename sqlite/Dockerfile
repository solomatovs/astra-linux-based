# sqlite
ARG RUNTIME_IMAGE=dmp/glibc:2.28
ARG BUILD_IMAGE=dmp/gcc:12.4.0-dist

FROM ${BUILD_IMAGE}     AS gcc
FROM ${RUNTIME_IMAGE}   AS base
FROM base AS builder

ARG SQLITE_VERSION
ARG SQLITE_AMALGAMATION
ARG SQLITE_PATH=/opt/sqlite
ARG GCC_PATH=/opt/gcc

# тулчейн из -dist образа gcc: компилятор, binutils и заголовки
COPY --from=gcc ${GCC_PATH}/usr/local/ /usr/local/
COPY --from=gcc /usr/include/ /usr/include/
RUN ln -sf gcc /usr/local/bin/cc            \
    && ln -sf g++ /usr/local/bin/c++        \
    && printf '%s\n'                        \
        /usr/local/lib64                    \
        /usr/local/lib                      \
        > /etc/ld.so.conf.d/gcc-local.conf  \
    && ldconfig

# readline даёт историю и редактирование строки в CLI sqlite3
RUN apt-get update                                              \
    && apt-get install -y --no-install-recommends               \
        libreadline-dev zlib1g-dev                              \
    && apt-get clean                                            \
    && rm -rf /var/lib/apt/lists/* /var/cache/apt/archives/*

# amalgamation-тарбол: один sqlite3.c + configure, внешних зависимостей нет
COPY artifacts/src/sqlite-autoconf-${SQLITE_AMALGAMATION}.tar.gz /tmp/src/sqlite.tar.gz

# набор SQLITE_ENABLE_* повторяет сборку libsqlite3 в hermes-agent: его
# state.db держит историю в FTS5 с tokenize='trigram'
RUN set -eux;                                                   \
    mkdir -p /tmp/build;                                        \
    tar -xf /tmp/src/sqlite.tar.gz -C /tmp/build;               \
    cd /tmp/build/sqlite-autoconf-${SQLITE_AMALGAMATION};       \
    CFLAGS="-O2                                                 \
        -DSQLITE_ENABLE_FTS3                                    \
        -DSQLITE_ENABLE_FTS3_PARENTHESIS                        \
        -DSQLITE_ENABLE_FTS4                                    \
        -DSQLITE_ENABLE_FTS5                                    \
        -DSQLITE_ENABLE_RTREE                                   \
        -DSQLITE_ENABLE_GEOPOLY                                 \
        -DSQLITE_ENABLE_COLUMN_METADATA                         \
        -DSQLITE_ENABLE_UNLOCK_NOTIFY                           \
        -DSQLITE_ENABLE_DBSTAT_VTAB                             \
        -DSQLITE_ENABLE_DBPAGE_VTAB                             \
        -DSQLITE_ENABLE_MATH_FUNCTIONS                          \
        -DSQLITE_ENABLE_PREUPDATE_HOOK                          \
        -DSQLITE_ENABLE_SESSION                                 \
        -DSQLITE_SECURE_DELETE                                  \
        -DSQLITE_THREADSAFE=1                                   \
        -DSQLITE_MAX_VARIABLE_NUMBER=250000"                    \
    ./configure                                                 \
        --prefix=/usr/local                                     \
        --disable-static                                        \
        --enable-readline;                                      \
    make -j"$(nproc)";                                          \
    make install DESTDIR=${SQLITE_PATH};                        \
    strip ${SQLITE_PATH}/usr/local/bin/sqlite3;                 \
    find ${SQLITE_PATH}/usr/local/lib -name 'libsqlite3.so.*.*' -exec strip --strip-unneeded {} +; \
    rm -rf ${SQLITE_PATH}/usr/local/share/man;                  \
    du -h --max-depth=2 ${SQLITE_PATH};                         \
    cd /;                                                       \
    rm -rf /tmp/build /tmp/src

# CLI линкуется с readline, которого нет в чистом runtime-образе
RUN set -eux;                                                   \
    for lib in $(ldd ${SQLITE_PATH}/usr/local/bin/sqlite3       \
        | awk '/libreadline|libtinfo|libncurses/ {print $3}'); do \
        cp -L "$lib" ${SQLITE_PATH}/usr/local/lib/;             \
    done


# dist: только собранный /opt/sqlite без сборочного тулчейна — компактный «дистрибутив»
FROM base AS dist
ARG SQLITE_PATH=/opt/sqlite
COPY --from=builder ${SQLITE_PATH} ${SQLITE_PATH}


FROM base AS runtime

ARG SQLITE_PATH=/opt/sqlite

# устанавливаем собранный sqlite в runtime-образ
COPY --from=dist ${SQLITE_PATH} /

# /usr/local/lib идёт первым в ld.so.conf, чтобы наша libsqlite3.so.0
# перекрывала системную (astra отдаёт 3.40.x)
RUN printf '%s\n'                                       \
        /usr/local/lib                                  \
        > /etc/ld.so.conf.d/000-sqlite-local.conf       \
    && ldconfig                                         \
    && ldconfig -p | grep -q '/usr/local/lib/libsqlite3.so.0'

ENTRYPOINT [ "/usr/local/bin/sqlite3" ]
