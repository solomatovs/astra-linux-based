# golang
ARG BUILD_IMAGE=dmp/gcc:8.5.0
ARG RUNTIME_IMAGE=dmp/glibc:2.28
ARG GOLANG_IMAGE=scratch

FROM ${GOLANG_IMAGE} AS bootstrap

FROM ${BUILD_IMAGE} AS dist
ARG GO_VERSION

COPY --from=bootstrap /out/usr/local/go /usr/local/go-bootstrap

WORKDIR /build
COPY artifacts/src/golang-${GO_VERSION}.tar.gz /build/
RUN tar xzf golang-${GO_VERSION}.tar.gz \
 && rm golang-${GO_VERSION}.tar.gz

WORKDIR /build/go-go${GO_VERSION}/src
RUN export GOROOT_BOOTSTRAP=/usr/local/go-bootstrap \
 && ./make.bash

RUN mkdir -p /out/usr/local/go \
 && cp -a /build/go-go${GO_VERSION}/bin \
          /build/go-go${GO_VERSION}/pkg \
          /build/go-go${GO_VERSION}/src \
          /out/usr/local/go/


FROM ${RUNTIME_IMAGE} AS runtime
COPY --from=dist /out/usr/local/go /usr/local/go

COPY artifacts/ca-chain.crt* /tmp/cacrt/
RUN if [ -f /tmp/cacrt/ca-chain.crt ] && grep -qE '^-----BEGIN CERTIFICATE-----' /tmp/cacrt/ca-chain.crt; then \
        cp /tmp/cacrt/ca-chain.crt /usr/local/share/ca-certificates/ca-chain.crt \
        && update-ca-certificates; \
    fi \
    && rm -rf /tmp/cacrt

COPY artifacts/go-env* /tmp/goenv/
RUN if [ -f /tmp/goenv/go-env ]; then mkdir -p /etc/go && cp /tmp/goenv/go-env /etc/go/env; fi \
    && rm -rf /tmp/goenv

ENV GOROOT=/usr/local/go \
    GOPATH=/go \
    GOENV=/etc/go/env \
    PATH=/usr/local/go/bin:/go/bin:${PATH}
ENTRYPOINT [ "/usr/local/go/bin/go" ]
