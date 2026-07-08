# runc
ARG BUILD_IMAGE=dmp/golang:1.22.6
ARG RUNTIME_IMAGE=dmp/glibc:2.28

FROM ${BUILD_IMAGE} AS base
FROM base AS dist

ARG RUNC_VERSION
ARG RUNC_BUILDTAGS=seccomp
ARG RUNC_PATH=/opt/runc

# build-toolchain
RUN apt-get update                                  \
    && apt-get install -y --no-install-recommends   \
        gcc make pkg-config libseccomp-dev          \
    && apt-get clean                                \
    && rm -rf /var/lib/apt/lists/* /var/cache/apt/archives/*

# runc
COPY artifacts/src/runc-${RUNC_VERSION}.tar.gz /tmp/runc.tar.gz
RUN set -eux;                                        \
    cd /tmp;                                         \
    tar -xf runc.tar.gz;                             \
    cd runc-${RUNC_VERSION};                         \
    export GOTOOLCHAIN=local GOFLAGS=-mod=vendor GOPROXY=off GO111MODULE=on CGO_ENABLED=1; \
    make BUILDTAGS="${RUNC_BUILDTAGS}" runc;         \
    install -D -m0755 runc ${RUNC_PATH}/usr/local/sbin/runc;    \
    if echo "${RUNC_BUILDTAGS}" | grep -qw seccomp; then        \
        mkdir -p ${RUNC_PATH}/usr/local/lib;        \
        cp -a /usr/lib/*/libseccomp.so.* ${RUNC_PATH}/usr/local/lib/ 2>/dev/null || \
        cp -a /lib/*/libseccomp.so.*     ${RUNC_PATH}/usr/local/lib/;               \
    fi;                                             \
    ${RUNC_PATH}/usr/local/sbin/runc --version;     \
    cd / && rm -rf /tmp/*


FROM ${RUNTIME_IMAGE} AS runtime

ARG RUNC_PATH=/opt/runc

COPY --from=dist ${RUNC_PATH} /
RUN printf '%s\n'                       \
        /usr/local/lib                  \
    > /etc/ld.so.conf.d/runc-local.conf \
    && ldconfig

ENTRYPOINT [ "/usr/local/sbin/runc" ]
