# docker-buildx
ARG BUILD_IMAGE=dmp/golang:1.24.5
ARG RUNTIME_IMAGE=dmp/glibc:2.28

FROM ${BUILD_IMAGE} AS base
FROM base AS dist

ARG BUILDX_VERSION
ARG BUILDX_REVISION=unknown
ARG BUILDX_PKG=github.com/docker/buildx
ARG BUILDX_PATH=/opt/docker-buildx

# buildx хранит vendor/ в репозитории, сборка офлайн
COPY artifacts/src/buildx-${BUILDX_VERSION}.tar.gz /tmp/buildx.tar.gz
RUN set -eux;                                       \
    cd /tmp;                                        \
    tar -xf buildx.tar.gz;                          \
    cd buildx-${BUILDX_VERSION};                    \
    export GOTOOLCHAIN=local GOFLAGS=-mod=vendor GOPROXY=off GO111MODULE=on CGO_ENABLED=0; \
    mkdir -p ${BUILDX_PATH}/usr/local/bin ${BUILDX_PATH}/usr/local/lib/docker/cli-plugins; \
    go build -trimpath                              \
        -ldflags "-s -w -X ${BUILDX_PKG}/version.Version=v${BUILDX_VERSION} -X ${BUILDX_PKG}/version.Revision=${BUILDX_REVISION}" \
        -o ${BUILDX_PATH}/usr/local/bin/docker-buildx ./cmd/buildx; \
    ln -s /usr/local/bin/docker-buildx              \
        ${BUILDX_PATH}/usr/local/lib/docker/cli-plugins/docker-buildx; \
    ${BUILDX_PATH}/usr/local/bin/docker-buildx version; \
    cd / && rm -rf /tmp/*


FROM ${RUNTIME_IMAGE} AS runtime

ARG BUILDX_PATH=/opt/docker-buildx

COPY --from=dist ${BUILDX_PATH} /

ENTRYPOINT [ "/usr/local/bin/docker-buildx" ]
