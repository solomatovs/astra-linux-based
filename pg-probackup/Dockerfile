# pg-probackup: утилита резервного копирования, собранная против исходников postgres
ARG PG_VERSION

FROM dmp/postgres:${PG_VERSION}-dist AS build

ARG PG_VERSION
ARG PROBACKUP_VERSION

# ставим postgres из staging в систему (pg_config, libpq, заголовки)
RUN cp -a /opt/pgsql/. /
ENV PATH=/usr/local/pgsql/bin:${PATH}

# pg_probackup собирается через PGXS + дерево исходников postgres (top_srcdir).
# исходникам нужен ./configure для генерации pg_config.h (полная сборка не требуется).
COPY artifacts/src/postgresql-${PG_VERSION}.tar.bz2 /tmp/pg.tar.bz2
COPY artifacts/src/pg_probackup-${PROBACKUP_VERSION}.tar.gz /tmp/pb.tar.gz
RUN cd /tmp                                                                  \
    && tar -xf pg.tar.bz2                                                    \
    && tar -xf pb.tar.gz                                                     \
    && ( cd postgresql-${PG_VERSION} && ./configure >/dev/null )            \
    && cd /tmp/pg_probackup-${PROBACKUP_VERSION}                             \
    && make USE_PGXS=1 PG_CONFIG=/usr/local/pgsql/bin/pg_config             \
        top_srcdir=/tmp/postgresql-${PG_VERSION}                            \
    && install -m755 pg_probackup /usr/local/pgsql/bin/pg_probackup         \
    && cd / && rm -rf /tmp/*


FROM dmp/postgres:${PG_VERSION} AS runtime

USER root
COPY --from=build /usr/local/pgsql/bin/pg_probackup /usr/local/pgsql/bin/pg_probackup
USER postgres
ENTRYPOINT [ "pg_probackup" ]
