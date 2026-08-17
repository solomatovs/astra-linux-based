# code-marketplace
ARG RUNTIME_IMAGE=dmp/glibc:2.28
ARG GO_IMAGE=dmp/golang:1.25.12-dist

# runtime берёт дистрибутив из готового -dist образа (--build-arg DIST_IMAGE),
# иначе стадия builder компилировалась бы заново
ARG DIST_IMAGE=scratch

FROM ${GO_IMAGE} AS go

FROM ${RUNTIME_IMAGE} AS base
FROM base AS builder

ARG CM_VERSION
ARG CM_PATH=/opt/code-marketplace
ARG GO_PATH=/opt/go

# тулчейн go из -dist образа: лежит деревом /opt/go/usr/local/go
COPY --from=go ${GO_PATH} /

# GOPROXY=off и -mod=vendor — сборка не идет в сеть, все зависимости уже в
# vendor-<версия>.tar.gz. GOTOOLCHAIN=local — иначе go качает тулчейн вместо
# того, чтобы упасть
ENV GOROOT=/usr/local/go              \
    GOPATH=/go                        \
    PATH=/usr/local/go/bin:/go/bin:${PATH} \
    CGO_ENABLED=0                     \
    GOFLAGS=-mod=vendor               \
    GOPROXY=off                       \
    GOTOOLCHAIN=local

COPY artifacts/src/ /tmp/src/
RUN cd /tmp                                                 \
    && tar -xf src/code-marketplace-${CM_VERSION}-src.tar.gz \
    && mv code-marketplace-${CM_VERSION} cm                 \
    && tar -xf src/vendor-${CM_VERSION}.tar.gz -C cm        \
    && cd cm                                                \
    && go build                                             \
        -trimpath                                           \
        -ldflags "-s -w -X github.com/coder/code-marketplace/buildinfo.tag=v${CM_VERSION}" \
        -o ${CM_PATH}/usr/local/bin/code-marketplace        \
        ./cmd/marketplace                                   \
    && ${CM_PATH}/usr/local/bin/code-marketplace version    \
    && du -h --max-depth=1 ${CM_PATH}                       \
    && cd /                                                 \
    && rm -rf /tmp/*


# dist: только собранный /opt/code-marketplace без тулчейна go — компактный «дистрибутив»
FROM base AS dist
ARG CM_PATH=/opt/code-marketplace
COPY --from=builder ${CM_PATH} ${CM_PATH}


FROM ${DIST_IMAGE} AS distimg

FROM base AS runtime

ARG CM_PATH=/opt/code-marketplace
ARG CM_UID=1000
ARG CM_GID=1000
ARG CM_EXT_DIR=/var/lib/code-marketplace/extensions

# устанавливаем собранный бинарник в runtime-образ
COPY --from=distimg ${CM_PATH} /

# ca-certificates — для `code-marketplace add https://...`, curl — для HEALTHCHECK
RUN apt-get update                                            \
    && apt-get install -y --no-install-recommends               \
        ca-certificates curl                                    \
    && apt-get clean                                            \
    && rm -rf /var/lib/apt/lists/* /var/cache/apt/archives/*

# непривилегированный пользователь, ему же принадлежит каталог расширений
RUN groupadd -g ${CM_GID} marketplace                                       \
    && useradd -u ${CM_UID} -g ${CM_GID} -m -d /home/marketplace            \
        -s /bin/bash marketplace                                            \
    && mkdir -p ${CM_EXT_DIR}                                               \
    && chown -R marketplace:marketplace ${CM_EXT_DIR} /home/marketplace

VOLUME [ "/var/lib/code-marketplace/extensions" ]
EXPOSE 3001

HEALTHCHECK --interval=30s --timeout=5s --start-period=5s --retries=3 \
    CMD curl -fsS http://127.0.0.1:3001/healthz || exit 1

USER marketplace
WORKDIR /home/marketplace

# ENTRYPOINT — сам бинарник, чтобы работали и подкоманды add/remove
ENTRYPOINT [ "/usr/local/bin/code-marketplace" ]
CMD [ "server", "--address", "0.0.0.0:3001", "--extensions-dir", "/var/lib/code-marketplace/extensions" ]
