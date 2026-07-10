# pgcat
ARG RUNTIME_IMAGE=dmp/glibc:2.28
ARG BUILD_IMAGE=dmp/rust:1.90.0-dist

FROM ${RUNTIME_IMAGE} AS base
FROM ${BUILD_IMAGE} AS builder

ARG PGCAT_VERSION
ARG APT_DEPS
ARG PGCAT_PATH=/opt/pgcat

# cargo build --offline
COPY artifacts/src/ /tmp/src/
RUN cd /tmp                                                     \
    && tar -xf src/pgcat-${PGCAT_VERSION}.tar.gz                \
    && cd pgcat-${PGCAT_VERSION}                                \
    && cargo build --release --offline                          \
    && mkdir -p ${PGCAT_PATH}/usr/local/bin                     \
    && cp -a target/release/pgcat ${PGCAT_PATH}/usr/local/bin/  \
    && du -h --max-depth=1 ${PGCAT_PATH}                        \
    && cd /                                                     \
    && rm -rf /tmp/*


# dist: только собранный /opt/pgcat
FROM base AS dist
ARG PGCAT_PATH=/opt/pgcat
COPY --from=builder ${PGCAT_PATH} ${PGCAT_PATH}


FROM base AS runtime

ARG PGCAT_PATH=/opt/pgcat
# устанавливаем собранный pgcat в runtime-образ
COPY --from=dist ${PGCAT_PATH} /

ENTRYPOINT [ "/usr/local/bin/pgcat" ]
