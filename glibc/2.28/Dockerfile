FROM registry.astralinux.ru/library/orel:2.12 AS pre_build

ARG GLIBC_PATH=/opt/glibc

RUN printf '%s\n'                                                   \
        'APT::Get::AllowUnauthenticated "true";'                    \
        'Acquire::AllowInsecureRepositories "true";'                \
        'Acquire::AllowDowngradeToInsecureRepositories "true";'     \
        > /etc/apt/apt.conf.d/99insecure                            \
    && printf '%s\n'                                                \
        'Dpkg::Options { "--force-confdef"; "--force-confold"; };'  \
        > /etc/apt/apt.conf.d/90noninteractive

COPY artifacts/apt-sources.list* /tmp/aptsrc/
RUN if [ -f /tmp/aptsrc/apt-sources.list ] && \
        grep -qvE '^[[:space:]]*(#|$)' /tmp/aptsrc/apt-sources.list; then \
        cp /tmp/aptsrc/apt-sources.list /etc/apt/sources.list; \
    fi \
    && rm -rf /tmp/aptsrc

COPY artifacts/ca-chain.crt* /tmp/cacrt/
RUN if [ -f /tmp/cacrt/ca-chain.crt ] && grep -qE '^-----BEGIN CERTIFICATE-----' /tmp/cacrt/ca-chain.crt; then \
        cp /tmp/cacrt/ca-chain.crt /usr/local/share/ca-certificates/ca-chain.crt \
        && update-ca-certificates; \
    fi \
    && rm -rf /tmp/cacrt

# Локально скачанные .deb (напр. apt-transport-https для https-репозитория в
# закрытом контуре). Кладутся в artifacts/apt-bootstrap/ через `make debs`.
# Ставятся ДО первого apt-get update, иначе https-метод недоступен.
COPY artifacts/apt-bootstrap* /tmp/debs/
RUN if ls /tmp/debs/*.deb >/dev/null 2>&1; then \
        echo ">>> устанавливаю bootstrap .deb:" && ls -1 /tmp/debs/*.deb \
        && dpkg -i /tmp/debs/*.deb; \
    fi \
    && rm -rf /tmp/debs


FROM pre_build AS glibc_compile

RUN apt-get update \
    && DEBIAN_FRONTEND=noninteractive           \
    apt-get install -y --no-install-recommends  \
        build-essential gawk bison xz-utils gettext \
    && rm -rf /var/lib/apt/lists/*

COPY artifacts/glibc-src/glibc-*.tar.xz /tmp/
RUN cd /tmp \
    && tar -xf glibc-*.tar.xz   \
    && mkdir build              \
    && cd build                 \
    && ../glibc-*/configure     \
        --prefix=/usr           \
        --libdir=/usr/lib/x86_64-linux-gnu      \
        --disable-sanity-checks --disable-werror\
        --enable-kernel=4.15                    \
        libc_cv_slibdir=/lib/x86_64-linux-gnu   \
        libc_cv_rtlddir=/lib64                  \
    && make -j"$(nproc)"                        \
    && make install PERL=/bin/true DESTDIR=${GLIBC_PATH} \
    && rm -rf ${GLIBC_PATH}/usr/share/i18n ${GLIBC_PATH}/usr/share/locale \
              ${GLIBC_PATH}/usr/share/doc  ${GLIBC_PATH}/usr/share/info   \
    && find ${GLIBC_PATH} -name '*.a' ! -name '*nonshared*' -delete \
    && find ${GLIBC_PATH} -type f -name '*.so*' -exec strip --strip-unneeded {} + 2>/dev/null || true \
    && rm -rf /tmp/*


FROM pre_build AS glibc

ARG GLIBC_PATH=/opt/glibc
COPY --from=glibc_compile ${GLIBC_PATH} /

RUN set -eux \
    && printf '%s\n' /usr/local/lib /usr/local/lib64 /lib /usr/lib > /etc/ld.so.conf.d/000-legacy-libdirs.conf \
    && ldconfig \
    && ldd --version | head -n1

RUN apt-get update \
    && DEBIAN_FRONTEND=noninteractive \
    apt-get install -y --no-install-recommends locales \
    && rm -rf /var/lib/apt/lists/* \
    && mkdir -p /usr/lib/x86_64-linux-gnu/locale \
    && localedef -i ru_RU -c -f UTF-8 ru_RU.UTF-8 \
    && locale -a | grep -q '^ru_RU\.utf8$'

ENV LANG=ru_RU.UTF-8 \
    LC_ALL=ru_RU.UTF-8 \
    LANGUAGE=ru_RU.UTF-8
