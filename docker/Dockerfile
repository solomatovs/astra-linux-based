# docker (dockerd + docker CLI), собирает полный движок вместе с runc и containerd
ARG BUILD_IMAGE=dmp/golang:1.22.6
ARG RUNTIME_IMAGE=dmp/glibc:2.28
ARG RUNC_IMAGE=dmp/runc:1.2.6-dist
ARG CONTAINERD_IMAGE=dmp/containerd:1.7.27-dist

FROM ${RUNC_IMAGE}          AS runc
FROM ${CONTAINERD_IMAGE}    AS containerd

FROM ${BUILD_IMAGE} AS base
FROM base AS dist

ARG DOCKER_VERSION
ARG DOCKER_GITCOMMIT=unknown
ARG DOCKER_BUILDTAGS="seccomp exclude_graphdriver_btrfs exclude_graphdriver_devicemapper"
ARG DOCKER_PATH=/opt/docker

# build-toolchain
RUN apt-get update                                  \
    && apt-get install -y --no-install-recommends   \
        gcc make pkg-config libseccomp-dev          \
    && apt-get clean                                \
    && rm -rf /var/lib/apt/lists/* /var/cache/apt/archives/*

ENV GOTOOLCHAIN=local GOFLAGS=-mod=vendor GOPROXY=off GO111MODULE=on

# dockerd + docker-proxy (moby)
COPY artifacts/src/moby-${DOCKER_VERSION}.tar.gz /tmp/moby.tar.gz
RUN set -eux;                                       \
    cd /tmp;                                        \
    tar -xf moby.tar.gz;                            \
    cd moby-${DOCKER_VERSION};                      \
    if [ -f vendor.mod ] && [ ! -f go.mod ]; then cp vendor.mod go.mod; cp vendor.sum go.sum; fi; \
    export CGO_ENABLED=1;                           \
    mkdir -p ${DOCKER_PATH}/usr/local/bin;          \
    go build -tags "${DOCKER_BUILDTAGS}" \
        -ldflags "-X github.com/docker/docker/dockerversion.Version=${DOCKER_VERSION} -X github.com/docker/docker/dockerversion.GitCommit=${DOCKER_GITCOMMIT}" \
        -o ${DOCKER_PATH}/usr/local/bin/dockerd ./cmd/dockerd; \
    if [ -d cmd/docker-proxy ]; then \
        CGO_ENABLED=0 go build -o ${DOCKER_PATH}/usr/local/bin/docker-proxy ./cmd/docker-proxy; \
    fi; \
    ${DOCKER_PATH}/usr/local/bin/dockerd --version; \
    cd / && rm -rf /tmp/*

# docker CLI (docker/cli)
COPY artifacts/src/cli-${DOCKER_VERSION}.tar.gz /tmp/cli.tar.gz
RUN set -eux;                                       \
    cd /tmp;                                        \
    tar -xf cli.tar.gz;                             \
    cd cli-${DOCKER_VERSION};                       \
    if [ -f vendor.mod ] && [ ! -f go.mod ]; then cp vendor.mod go.mod; cp vendor.sum go.sum; fi; \
    export CGO_ENABLED=0;                           \
    go build \
        -ldflags "-X github.com/docker/cli/cli/version.Version=${DOCKER_VERSION} -X github.com/docker/cli/cli/version.GitCommit=${DOCKER_GITCOMMIT}" \
        -o ${DOCKER_PATH}/usr/local/bin/docker ./cmd/docker; \
    ${DOCKER_PATH}/usr/local/bin/docker --version;  \
    cd / && rm -rf /tmp/*


FROM ${RUNTIME_IMAGE} AS runtime

ARG DOCKER_PATH=/opt/docker
ARG RUNC_PATH=/opt/runc
ARG CONTAINERD_PATH=/opt/containerd

COPY --from=dist       ${DOCKER_PATH}       /
COPY --from=containerd ${CONTAINERD_PATH}   /
COPY --from=runc       ${RUNC_PATH}         /

RUN printf '%s\n'           \
        /usr/local/lib      \
    > /etc/ld.so.conf.d/docker-local.conf \
    && ldconfig

ENTRYPOINT [ "/usr/local/bin/dockerd" ]
