# pgaudit
ARG PG_VERSION
ARG BASE_IMAGE=dmp/postgres:${PG_VERSION}

FROM dmp/postgres:${PG_VERSION}-dist AS build

ARG EXT_VERSION

# ставим postgres из staging в систему, чтобы pg_config/заголовки резолвились
RUN cp -a /opt/pgsql/. /
ENV PATH=/usr/local/pgsql/bin:${PATH}

COPY artifacts/src/pgaudit-${EXT_VERSION}.tar.gz /tmp/src.tar.gz
COPY build.sh /tmp/build.sh
RUN chmod +x /tmp/build.sh && /tmp/build.sh


FROM ${BASE_IMAGE} AS runtime

USER root
COPY --from=build /opt/ext/ /
# pgaudit работает только из shared_preload_libraries, свой entrypoint прописывает его при initdb
COPY docker-entrypoint.sh /usr/local/bin/docker-entrypoint.sh
RUN chmod +x /usr/local/bin/docker-entrypoint.sh && ldconfig
USER postgres
