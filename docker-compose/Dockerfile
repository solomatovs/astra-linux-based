# docker-compose
ARG BUILD_IMAGE=dmp/golang:1.24.5
ARG RUNTIME_IMAGE=dmp/glibc:2.28

FROM ${BUILD_IMAGE} AS base
FROM base AS dist

ARG COMPOSE_VERSION
ARG COMPOSE_PKG=github.com/docker/compose/v2
ARG COMPOSE_PATH=/opt/docker-compose

# исходники приходят с vendor/ (создан на этапе make sources), сборка офлайн
COPY artifacts/src/compose-${COMPOSE_VERSION}-vendored.tar.gz /tmp/compose.tar.gz
RUN set -eux;                                       \
    cd /tmp;                                        \
    tar -xf compose.tar.gz;                         \
    cd compose-${COMPOSE_VERSION};                  \
    export GOTOOLCHAIN=local GOFLAGS=-mod=vendor GOPROXY=off GO111MODULE=on CGO_ENABLED=0;      \
    mkdir -p ${COMPOSE_PATH}/usr/local/bin ${COMPOSE_PATH}/usr/local/lib/docker/cli-plugins;    \
    go build -trimpath                              \
        -ldflags "-s -w -X ${COMPOSE_PKG}/internal.Version=v${COMPOSE_VERSION}" \
        -o ${COMPOSE_PATH}/usr/local/bin/docker-compose ./cmd;                  \
    ln -s /usr/local/bin/docker-compose                                         \
        ${COMPOSE_PATH}/usr/local/lib/docker/cli-plugins/docker-compose;        \
    ${COMPOSE_PATH}/usr/local/bin/docker-compose version;                       \
    cd / && rm -rf /tmp/*


FROM ${RUNTIME_IMAGE} AS runtime

ARG COMPOSE_PATH=/opt/docker-compose

COPY --from=dist ${COMPOSE_PATH} /

ENTRYPOINT [ "/usr/local/bin/docker-compose" ]
