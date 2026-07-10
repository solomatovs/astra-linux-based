# postgres
ARG BUILD_IMAGE=dmp/gcc:8.5.0-dist
ARG RUNTIME_IMAGE=dmp/glibc:2.28

FROM ${BUILD_IMAGE} AS dist

ARG PG_VERSION
ARG PG_EXTRA_CONF="--with-libxml --with-readline --with-zlib"
ARG APT_DEPS
ARG PG_PREFIX=/usr/local/pgsql
ARG PG_PATH=/opt/pgsql

# gcc-тулчейн из slim gcc:*-dist лежит в /opt/gcc — переносим в /usr/local
RUN cp -a /opt/gcc/usr/local/. /usr/local/                          \
    && ln -sf gcc /usr/local/bin/cc                                 \
    && ln -sf g++ /usr/local/bin/c++                                \
    && printf '%s\n' /usr/local/lib64 /usr/local/lib > /etc/ld.so.conf.d/gcc-local.conf \
    && ldconfig


# build-toolchain
RUN apt-get update                  \
    && apt-get install -y --no-install-recommends \
        ${APT_DEPS}                 \
    && apt-get clean                \
    && rm -rf                       \
        /var/lib/apt/lists/*        \
        /var/cache/apt/archives/*

# MIT krb5 (для GSSAPI/Kerberos в postgres) — как в samba-ad, shared, в /usr/local.
# --disable-pkinit: единственный компонент krb5, которому нужен OpenSSL (не тянем второй).
ARG KRB5_VERSION
COPY artifacts/src/krb5-${KRB5_VERSION}.tar.gz /tmp/krb5.tar.gz
RUN cd /tmp                             \
    && tar xf krb5.tar.gz               \
    && cd krb5-${KRB5_VERSION}/src      \
    && ./configure                      \
        --prefix=/usr/local             \
        --disable-rpath                 \
        --disable-pkinit                \
    && make -j"$(nproc)"                \
    && make install                     \
    && ldconfig                         \
    && cd / && rm -rf /tmp/*

# PostgreSQL: ядро сервера + contrib + pl-языки (без docs)
# libieee.a — пустая заглушка: tclConfig.sh в Astra тянет устаревшую -lieee (нет в glibc >=2.23)
COPY artifacts/src/postgresql-${PG_VERSION}.tar.bz2 /tmp/pg.tar.bz2
RUN cd /tmp                                                          \
    && tar -xf pg.tar.bz2                                            \
    && cd postgresql-${PG_VERSION}                                   \
    && export PYTHON=/usr/bin/python3.7                              \
    && ar rcs /usr/lib/x86_64-linux-gnu/libieee.a                   \
    && CPPFLAGS="-I/usr/local/include"                               \
       LDFLAGS="-L/usr/local/lib -Wl,-rpath,/usr/local/lib"          \
       ./configure --prefix=${PG_PREFIX} ${PG_EXTRA_CONF}            \
    && make -j"$(nproc)"                                             \
    && make -j"$(nproc)" -C contrib                                  \
    && make install         DESTDIR=${PG_PATH}                       \
    && make -C contrib install DESTDIR=${PG_PATH}                    \
    && mkdir -p ${PG_PATH}/usr/local/lib                             \
    && for p in libgssapi_krb5 libkrb5 libk5crypto libcom_err        \
                libkrb5support libverto; do                          \
         cp -a /usr/local/lib/$p.so* ${PG_PATH}/usr/local/lib/ 2>/dev/null || true; \
       done                                                          \
    && cp -a /usr/local/lib/krb5 ${PG_PATH}/usr/local/lib/ 2>/dev/null || true \
    && cd / && rm -rf /tmp/*


FROM ${RUNTIME_IMAGE} AS runtime

ARG PG_PREFIX=/usr/local/pgsql
ARG PG_PATH=/opt/pgsql
ARG RUNTIME_APT_DEPS
ARG PGDATA=/var/lib/postgresql/data

RUN if [ -n "${RUNTIME_APT_DEPS}" ]; then                                   \
        apt-get update                                                      \
        && apt-get install -y --no-install-recommends ${RUNTIME_APT_DEPS}   \
        && apt-get clean                                                    \
        && rm -rf /var/lib/apt/lists/* /var/cache/apt/archives/*;           \
    fi

# сервер и его библиотеки из dist-стадии
COPY --from=dist ${PG_PATH} /
RUN printf '%s\n%s\n' "${PG_PREFIX}/lib" "/usr/local/lib" > /etc/ld.so.conf.d/postgres.conf \
    && ldconfig

# пользователь postgres (сервер не стартует от root)
RUN groupadd -r postgres --gid=999                                          \
    && useradd -r -g postgres --uid=999 --home-dir=/var/lib/postgresql      \
        --shell=/bin/bash postgres                                          \
    && mkdir -p ${PGDATA} /var/run/postgresql                               \
    && chown -R postgres:postgres /var/lib/postgresql /var/run/postgresql   \
    && chmod 2777 /var/run/postgresql

COPY docker-entrypoint.sh /usr/local/bin/
RUN chmod +x /usr/local/bin/docker-entrypoint.sh

ENV PATH=${PG_PREFIX}/bin:${PATH}   \
    PGDATA=${PGDATA}                \
    LANG=ru_RU.UTF-8

EXPOSE 5432
VOLUME ["/var/lib/postgresql/data"]
USER postgres
ENTRYPOINT [ "docker-entrypoint.sh" ]
CMD [ "postgres" ]
