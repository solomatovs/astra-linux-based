# postgres-ext: расширения поверх базового образа postgres
ARG PG_VERSION

# build: собираем расширения тулчейном из postgres:*-dist
FROM dmp/postgres:${PG_VERSION}-dist AS build

ARG PG_MAJOR

# ставим postgres из staging в систему, чтобы pg_config/заголовки резолвились
RUN cp -a /opt/pgsql/. /
ENV PATH=/usr/local/pgsql/bin:${PATH}

# cmake нужен для timescaledb; остальное (компилятор, dev-заголовки) уже в -dist
RUN apt-get update                                                          \
    && apt-get install -y --no-install-recommends cmake git                 \
    && apt-get clean && rm -rf /var/lib/apt/lists/* /var/cache/apt/archives/*

COPY artifacts/src/ /tmp/src/
COPY build-ext.sh /tmp/build-ext.sh
RUN chmod +x /tmp/build-ext.sh && /tmp/build-ext.sh ${PG_MAJOR}


# runtime: базовый postgres + установленные расширения
FROM dmp/postgres:${PG_VERSION} AS runtime

# список shared_preload_libraries для этого major (вычисляется в Makefile)
ARG PG_EXT_PRELOAD=pgaudit,pg_cron
ENV PG_EXT_PRELOAD=${PG_EXT_PRELOAD}

USER root
COPY --from=build /opt/ext/ /
# entrypoint с авто-preload (заменяет базовый; путь ENTRYPOINT наследуется)
COPY docker-entrypoint.sh /usr/local/bin/docker-entrypoint.sh
RUN chmod +x /usr/local/bin/docker-entrypoint.sh && ldconfig
USER postgres
