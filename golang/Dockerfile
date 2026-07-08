# golang
ARG RUNTIME_IMAGE=dmp/glibc:2.28
ARG BUILD_IMAGE=dmp/glibc:2.28

FROM ${BUILD_IMAGE} AS prev
ARG GO_PATH=/opt/go

# надо проверить prev образ, существует ли там новый скомпилированный go /opt/go
# если он есть, то копируем go в папку /seed
# так как BUILD_IMAGE может быть двух вариантов:
# 1. базовый glibc образ
# 2. предыдущий golang образ
RUN mkdir -p /seed \
    && if [ -d "${GO_PATH}" ] && [ -n "$(ls -A "${GO_PATH}" 2>/dev/null)" ]; then \
        cp -a "${GO_PATH}/." /seed/; \
    fi

FROM ${RUNTIME_IMAGE} AS base
FROM base AS builder

ARG GO_VERSION
ARG APT_DEPS
ARG GO_PATH=/opt/go


# копируем go из предыдущего образа если он есть в этом образе
# и в зависиомсти от того есть ли go bootstrap, то ставим разный набор apt зависимостей
COPY --from=prev /seed/ /
RUN if [ -x /usr/local/go/bin/go ]; then                            \
        :;                                                          \
    else                                                            \
        apt-get update                                              \
        && apt-get install -y --no-install-recommends               \
            gcc make libc6-dev                                      \
        && apt-get clean                                            \
        && rm -rf /var/lib/apt/lists/* /var/cache/apt/archives/*    \
        ;                                                           \
    fi

# GO
COPY artifacts/src/ /tmp/src/
RUN cd /tmp                                         \
    && tar -xf src/golang-${GO_VERSION}.tar.gz      \
    && cd go-go${GO_VERSION}/src                    \
    && export CGO_ENABLED=0                         \
    && if [ -x /usr/local/go/bin/go ]; then         \
        export GOROOT_BOOTSTRAP=/usr/local/go;      \
    else                                            \
        sed -i '/"-Werror",/d' cmd/dist/build.c;    \
    fi                                              \
    && ./make.bash                                  \
    && cd /tmp/go-go${GO_VERSION}                   \
    && mkdir -p ${GO_PATH}/usr/local/go             \
    && cp -a bin pkg src ${GO_PATH}/usr/local/go/   \
    && du -h --max-depth=1 ${GO_PATH}               \
    && cd /                                         \
    && rm -rf /tmp/*


# dist: только собранный /opt/go без bootstrap-тулчейна — компактный «дистрибутив»
FROM base AS dist
ARG GO_PATH=/opt/go
COPY --from=builder ${GO_PATH} ${GO_PATH}


FROM base AS runtime

ARG GO_PATH=/opt/go
# устанавливаем собранный go в runtime-образ, чтобы собирать go-приложения
COPY --from=dist ${GO_PATH} /

COPY go-env* /tmp/goenv/
RUN if [ -f /tmp/goenv/go-env ]; then           \
        mkdir -p /etc/go                        \
        && cp /tmp/goenv/go-env /etc/go/env;    \
    fi                                          \
    && rm -rf /tmp/goenv

ENV GOROOT=/usr/local/go \
    GOPATH=/go \
    GOENV=/etc/go/env \
    PATH=/usr/local/go/bin:/go/bin:${PATH}

ENTRYPOINT [ "/usr/local/go/bin/go" ]
