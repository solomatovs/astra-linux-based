# samba-ad
ARG BASE_IMAGE=dmp/gcc:8.5.0
ARG PYTHON_IMAGE=dmp/python:3.11-dist
ARG RUNTIME_IMAGE=dmp/glibc:2.28

FROM ${RUNTIME_IMAGE} AS glibc
FROM ${PYTHON_IMAGE}  AS python
FROM ${BASE_IMAGE}    AS base
FROM base AS dist

ARG NETTLE_VERSION
ARG GNUTLS_VERSION
ARG KRB5_VERSION
ARG SAMBA_VERSION
ARG PYTHON_PATH=/opt/python
ARG SAMBA_PATH=/opt/samba
ARG ROOTFS=/opt/rootfs
ARG PIP_PACKAGES
ARG APT_DEPS

ENV PATH=/usr/local/bin:/usr/local/sbin:$PATH
ENV PKG_CONFIG_PATH=/usr/local/lib/pkgconfig:/usr/local/lib64/pkgconfig

# python 3.11 из dmp/python (нужен samba waf: python3 + модули)
COPY --from=python ${PYTHON_PATH}/ /
RUN ldconfig

COPY artifacts/apt-sources.list* /tmp/aptsrc/
RUN if [ -f /tmp/aptsrc/apt-sources.list ] && grep -qvE '^[[:space:]]*(#|$)' /tmp/aptsrc/apt-sources.list; then \
        cp /tmp/aptsrc/apt-sources.list /etc/apt/sources.list; \
    fi \
    && rm -rf /tmp/aptsrc

COPY artifacts/ca-chain.crt* /tmp/cacrt/
RUN if [ -f /tmp/cacrt/ca-chain.crt ] && grep -qE '^-----BEGIN CERTIFICATE-----' /tmp/cacrt/ca-chain.crt; then \
        cp /tmp/cacrt/ca-chain.crt /usr/local/share/ca-certificates/ca-chain.crt \
        && update-ca-certificates; \
    fi \
    && rm -rf /tmp/cacrt

# dev-зависимости samba (gcc/make/binutils/python уже в образах — их тут нет)
RUN apt-get update \
    && apt-get install -y --no-install-recommends ${APT_DEPS} \
    && apt-get clean \
    && rm -rf /var/lib/apt/lists/* /var/cache/apt/archives/*

# pip.conf (закрытый контур) + python-модули для samba
COPY artifacts/pip.conf* /tmp/pipconf/
RUN if [ -f /tmp/pipconf/pip.conf ]; then cp /tmp/pipconf/pip.conf /etc/pip.conf; fi \
    && rm -rf /tmp/pipconf \
    && pip3 install --no-cache-dir ${PIP_PACKAGES}

# nettle
COPY artifacts/src/nettle-${NETTLE_VERSION}.tar.gz /tmp/nettle.tar.gz
RUN cd /tmp \
    && tar xf nettle.tar.gz \
    && cd nettle-${NETTLE_VERSION} \
    && ./configure \
        --prefix=/usr/local \
        --disable-static \
        --enable-shared \
    && make -j"$(nproc)" \
    && make install \
    && ldconfig \
    && cd / && rm -rf /tmp/*

# GnuTLS
COPY artifacts/src/gnutls-${GNUTLS_VERSION}.tar.xz /tmp/gnutls.tar.xz
RUN cd /tmp \
    && tar xf gnutls.tar.xz \
    && cd gnutls-${GNUTLS_VERSION} \
    && ./configure \
        --prefix=/usr/local \
        --with-included-unistring \
        --without-p11-kit \
        --disable-doc \
        --disable-tests \
        --disable-tools \
        --disable-static \
    && make -j"$(nproc)" \
    && make install \
    && ldconfig \
    && cd / && rm -rf /tmp/*

# MIT krb5
COPY artifacts/src/krb5-${KRB5_VERSION}.tar.gz /tmp/krb5.tar.gz
RUN cd /tmp \
    && tar xf krb5.tar.gz \
    && cd krb5-${KRB5_VERSION}/src \
    && ./configure \
        --prefix=/usr/local \
        --disable-rpath \
    && make -j"$(nproc)" \
    && make install \
    && ldconfig \
    && ln -sf /usr/local/sbin/krb5kdc /usr/sbin/krb5kdc \
    && ln -sf /usr/local/sbin/kadmind /usr/sbin/kadmind \
    && cd / && rm -rf /tmp/*

# samba 4.21 AD DC (устанавливаем в DESTDIR, затем собираем финальный rootfs)
COPY artifacts/src/samba-${SAMBA_VERSION}.tar.gz /tmp/samba.tar.gz
RUN cd /tmp \
    && tar xf samba.tar.gz \
    && cd samba-${SAMBA_VERSION} \
    && ./configure \
        --prefix=/usr \
        --enable-fhs \
        --sysconfdir=/etc \
        --localstatedir=/var \
        --with-privatedir=/var/lib/samba/private \
        --bundled-libraries=ALL \
        --with-system-mitkrb5 \
        --with-experimental-mit-ad-dc \
        --without-systemd \
        --disable-cups \
        --without-pam \
    && make -j"$(nproc)" \
    && make install DESTDIR=${SAMBA_PATH} \
    && cd / && rm -rf /tmp/*

# Финальный rootfs
RUN set -eux \
    && mkdir -p ${ROOTFS}/usr/local \
    && cp -a ${SAMBA_PATH}/. ${ROOTFS}/ \
    && rm -rf ${ROOTFS}/var/lock ${ROOTFS}/var/run \
    && for d in lib lib64 bin sbin; do \
         [ -e /usr/local/$d ] && cp -a /usr/local/$d ${ROOTFS}/usr/local/ || true; \
       done \
    && rm -rf ${ROOTFS}/usr/local/lib/gcc \
              ${ROOTFS}/usr/local/libexec \
              ${ROOTFS}/usr/local/include \
    && rm -f  ${ROOTFS}/usr/local/bin/*gcc* \
              ${ROOTFS}/usr/local/bin/*g++* \
              ${ROOTFS}/usr/local/bin/c++ \
              ${ROOTFS}/usr/local/bin/cpp \
              ${ROOTFS}/usr/local/bin/gcov* \
              ${ROOTFS}/usr/local/bin/lto-dump \
    && find ${ROOTFS}/usr/local -name '*.a'  -delete \
    && find ${ROOTFS}/usr/local -name '*.la' -delete \
    && find ${ROOTFS} -type f -name '*.so*' -exec strip --strip-unneeded {} + 2>/dev/null || true \
    && find ${ROOTFS} -type f \( -path '*/bin/*' -o -path '*/sbin/*' \) -exec strip {} + 2>/dev/null || true \
    && du -h --max-depth=1 ${ROOTFS}


FROM ${RUNTIME_IMAGE} AS runtime

ARG ROOTFS=/opt/rootfs
ARG RUNTIME_APT_DEPS

ENV PATH=/usr/local/bin:/usr/local/sbin:$PATH

COPY artifacts/apt-sources.list* /tmp/aptsrc/
RUN if [ -f /tmp/aptsrc/apt-sources.list ] && grep -qvE '^[[:space:]]*(#|$)' /tmp/aptsrc/apt-sources.list; then \
        cp /tmp/aptsrc/apt-sources.list /etc/apt/sources.list; \
    fi \
    && rm -rf /tmp/aptsrc

COPY artifacts/ca-chain.crt* /tmp/cacrt/
RUN if [ -f /tmp/cacrt/ca-chain.crt ] && grep -qE '^-----BEGIN CERTIFICATE-----' /tmp/cacrt/ca-chain.crt; then \
        cp /tmp/cacrt/ca-chain.crt /usr/local/share/ca-certificates/ca-chain.crt \
        && update-ca-certificates; \
    fi \
    && rm -rf /tmp/cacrt

# рантайм-зависимости AD DC
RUN apt-get update \
    && apt-get install -y --no-install-recommends ${RUNTIME_APT_DEPS} \
    && apt-get clean \
    && rm -rf /var/lib/apt/lists/* /var/cache/apt/archives/*

COPY --from=dist ${ROOTFS}/ /
COPY ad/entrypoint.sh /usr/local/bin/entrypoint.sh

RUN set -eux \
    && printf '%s\n' /usr/local/lib /usr/local/lib64 /lib /usr/lib > /etc/ld.so.conf.d/000-runtime.conf \
    && ldconfig \
    && ln -sf /usr/local/sbin/krb5kdc /usr/sbin/krb5kdc \
    && ln -sf /usr/local/sbin/kadmind /usr/sbin/kadmind \
    && rm -f /etc/samba/smb.conf \
    && samba --version \
    && chmod +x /usr/local/bin/entrypoint.sh \
    && mkdir -p /docker-entrypoint-init.d

EXPOSE 53/tcp 53/udp 88/tcp 88/udp 464/tcp 464/udp 389/tcp 636/tcp 3268/tcp 3269/tcp

ENTRYPOINT ["/usr/local/bin/entrypoint.sh"]
