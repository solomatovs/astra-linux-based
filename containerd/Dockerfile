# containerd
ARG BUILD_IMAGE=dmp/golang:1.22.6
ARG RUNTIME_IMAGE=dmp/glibc:2.28

FROM ${BUILD_IMAGE} AS base
FROM base AS dist

ARG CONTAINERD_VERSION
ARG CONTAINERD_BUILDTAGS=no_btrfs
ARG CONTAINERD_PATH=/opt/containerd

# containerd
COPY artifacts/src/containerd-${CONTAINERD_VERSION}.tar.gz /tmp/containerd.tar.gz
RUN set -eux;                                       \
    cd /tmp;                                        \
    tar -xf containerd.tar.gz;                      \
    cd containerd-${CONTAINERD_VERSION};            \
    export GOTOOLCHAIN=local GOFLAGS=-mod=vendor GOPROXY=off GO111MODULE=on CGO_ENABLED=0; \
    MOD="$(go list -m)";                            \
    mkdir -p ${CONTAINERD_PATH}/usr/local/bin;      \
    go build -tags "${CONTAINERD_BUILDTAGS}" -ldflags "-s -w -X \$MOD/version.Version=v${CONTAINERD_VERSION}" -o ${CONTAINERD_PATH}/usr/local/bin/containerd              ./cmd/containerd;              \
    go build -tags "${CONTAINERD_BUILDTAGS}" -ldflags "-s -w -X \$MOD/version.Version=v${CONTAINERD_VERSION}" -o ${CONTAINERD_PATH}/usr/local/bin/containerd-shim-runc-v2 ./cmd/containerd-shim-runc-v2; \
    go build -tags "${CONTAINERD_BUILDTAGS}" -ldflags "-s -w -X \$MOD/version.Version=v${CONTAINERD_VERSION}" -o ${CONTAINERD_PATH}/usr/local/bin/ctr                     ./cmd/ctr;                     \
    ${CONTAINERD_PATH}/usr/local/bin/containerd --version; \
    cd / && rm -rf /tmp/*


FROM ${RUNTIME_IMAGE} AS runtime

ARG CONTAINERD_PATH=/opt/containerd

COPY --from=dist ${CONTAINERD_PATH} /

ENTRYPOINT [ "/usr/local/bin/containerd" ]
