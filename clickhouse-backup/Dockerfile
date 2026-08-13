# clickhouse-backup
ARG RUNTIME_IMAGE=dmp/glibc:2.28
ARG GO_IMAGE=dmp/golang:1.26.5-dist

FROM ${GO_IMAGE} AS go

FROM ${RUNTIME_IMAGE} AS base
FROM base AS builder

ARG CB_VERSION
ARG CB_PATH=/opt/clickhouse-backup
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
RUN cd /tmp                                                     \
    && tar -xf src/clickhouse-backup-${CB_VERSION}-src.tar.gz   \
    && mv clickhouse-backup-${CB_VERSION} cb                    \
    && tar -xf src/vendor-${CB_VERSION}.tar.gz -C cb            \
    && cd cb                                                    \
    && go build                                                 \
        -trimpath                                               \
        -buildvcs=false                                         \
        -ldflags "-s -w -X 'main.version=${CB_VERSION}'"        \
        -o ${CB_PATH}/usr/local/bin/clickhouse-backup           \
        ./cmd/clickhouse-backup                                 \
    && ${CB_PATH}/usr/local/bin/clickhouse-backup --version     \
    && du -h --max-depth=1 ${CB_PATH}                           \
    && cd /                                                     \
    && rm -rf /tmp/*


# dist: только собранный /opt/clickhouse-backup без тулчейна go — компактный «дистрибутив»
FROM base AS dist
ARG CB_PATH=/opt/clickhouse-backup
COPY --from=builder ${CB_PATH} ${CB_PATH}


FROM base AS runtime

ARG CB_PATH=/opt/clickhouse-backup

# устанавливаем собранный бинарник в runtime-образ
COPY --from=dist ${CB_PATH} /

# ca-certificates — для выгрузки в S3/GCS/Azure по https, curl — для HEALTHCHECK
RUN apt-get update                                              \
    && apt-get install -y --no-install-recommends               \
        ca-certificates curl                                    \
    && apt-get clean                                            \
    && rm -rf /var/lib/apt/lists/* /var/cache/apt/archives/*

# конфиг по умолчанию, чтобы образ запускался без монтирования своего
RUN mkdir -p /etc/clickhouse-backup                             \
    && /usr/local/bin/clickhouse-backup default-config          \
        > /etc/clickhouse-backup/config.yml

# работает от root: нужен доступ к /var/lib/clickhouse сервера, каталог
# принадлежит пользователю clickhouse-server, а не бэкаперу
ENV CLICKHOUSE_BACKUP_CONFIG=/etc/clickhouse-backup/config.yml

# порт REST API (clickhouse-backup server) с метриками для prometheus
EXPOSE 7171

HEALTHCHECK --interval=30s --timeout=5s --start-period=5s --retries=3 \
    CMD curl -fsS http://127.0.0.1:7171/backup/status || exit 1

# ENTRYPOINT — сам бинарник, чтобы работали подкоманды create/upload/restore
ENTRYPOINT [ "/usr/local/bin/clickhouse-backup" ]
