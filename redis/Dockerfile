# redis
ARG BUILD_IMAGE=dmp/gcc:8.5.0-dist
ARG RUNTIME_IMAGE=dmp/glibc:2.28

FROM ${BUILD_IMAGE} AS base
FROM base AS dist

ARG REDIS_VERSION
ARG BUILD_TLS=yes
ARG APT_DEPS
ARG REDIS_PREFIX=/usr/local
ARG REDIS_PATH=/opt/redis

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

# Redis: сервер + утилиты
COPY artifacts/src/redis-${REDIS_VERSION}.tar.gz /tmp/redis.tar.gz
RUN cd /tmp                                                        \
    && tar -xf redis.tar.gz                                        \
    && cd redis-${REDIS_VERSION}                                   \
    && make -j"$(nproc)" BUILD_TLS=${BUILD_TLS}                    \
    && make install PREFIX=${REDIS_PATH}${REDIS_PREFIX}            \
    && cd / && rm -rf /tmp/*


FROM ${RUNTIME_IMAGE} AS runtime

ARG REDIS_PREFIX=/usr/local
ARG REDIS_PATH=/opt/redis
ARG RUNTIME_APT_DEPS
ARG REDIS_DIR=/data

RUN if [ -n "${RUNTIME_APT_DEPS}" ]; then                                   \
        apt-get update                                                      \
        && apt-get install -y --no-install-recommends ${RUNTIME_APT_DEPS}   \
        && apt-get clean                                                    \
        && rm -rf /var/lib/apt/lists/* /var/cache/apt/archives/*;           \
    fi

# сервер и его библиотеки из dist-стадии
COPY --from=dist ${REDIS_PATH} /
RUN ldconfig

# пользователь redis
RUN groupadd -r redis --gid=999                                                 \
    && useradd -r -g redis --uid=999 --home-dir=/data --shell=/bin/bash redis   \
    && mkdir -p ${REDIS_DIR}                                                    \
    && chown -R redis:redis ${REDIS_DIR}

COPY docker-entrypoint.sh /usr/local/bin/
RUN chmod +x /usr/local/bin/docker-entrypoint.sh

ENV PATH=${REDIS_PREFIX}/bin:${PATH}    \
    REDIS_DIR=${REDIS_DIR}              \
    LANG=ru_RU.UTF-8

EXPOSE 6379
VOLUME ["/data"]
WORKDIR /data
USER redis
ENTRYPOINT [ "docker-entrypoint.sh" ]
CMD [ "redis-server" ]
