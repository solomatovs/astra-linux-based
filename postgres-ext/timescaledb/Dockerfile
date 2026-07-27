# timescaledb (apache-only)
ARG PG_VERSION
ARG BASE_IMAGE=dmp/postgres:${PG_VERSION}

FROM dmp/postgres:${PG_VERSION}-dist AS build

ARG EXT_VERSION

# ставим postgres из staging в систему, чтобы pg_config/заголовки резолвились
RUN cp -a /opt/pgsql/. /
ENV PATH=/usr/local/pgsql/bin:${PATH}

# timescaledb собирается cmake, а не PGXS
RUN apt-get update                                                          \
    && apt-get install -y --no-install-recommends cmake git                 \
    && apt-get clean && rm -rf /var/lib/apt/lists/* /var/cache/apt/archives/*

COPY artifacts/src/timescaledb-${EXT_VERSION}.tar.gz /tmp/src.tar.gz
COPY build.sh /tmp/build.sh
RUN chmod +x /tmp/build.sh && /tmp/build.sh


FROM ${BASE_IMAGE} AS runtime

USER root
COPY --from=build /opt/ext/ /
# timescaledb работает только из shared_preload_libraries, свой entrypoint прописывает его при initdb
COPY docker-entrypoint.sh /usr/local/bin/docker-entrypoint.sh
RUN chmod +x /usr/local/bin/docker-entrypoint.sh && ldconfig
USER postgres
