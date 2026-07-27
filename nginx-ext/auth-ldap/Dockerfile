# auth-ldap: аутентификация в LDAP/AD (kvspb/nginx-auth-ldap), поверх образа nginx
ARG NGINX_VERSION
ARG BASE_IMAGE=dmp/nginx:${NGINX_VERSION}

# build: собираем модуль в дереве исходников из nginx:*-dist
FROM dmp/nginx:${NGINX_VERSION}-dist AS build

ARG OPENLDAP_VERSION
ARG MODULE_REF

# OpenLDAP client (-lldap -llber): нужен и на сборке, и в рантайме
COPY artifacts/src/openldap-${OPENLDAP_VERSION}.tgz /tmp/openldap.tgz
RUN set -eux;                               \
    cd /tmp;                                \
    tar xf openldap.tgz;                    \
    cd openldap-${OPENLDAP_VERSION};        \
    ./configure                             \
        --prefix=/usr/local                 \
        --disable-slapd --disable-static    \
        --without-tls --without-cyrus-sasl --without-systemd; \
    make depend;                            \
    make -j"$(nproc)" -C include;           \
    make -j"$(nproc)" -C libraries;         \
    make -C include install;                \
    make -C libraries install;              \
    ldconfig;                               \
    cd /; rm -rf /tmp/openldap*

COPY artifacts/src/nginx-auth-ldap-${MODULE_REF}.tar.gz /tmp/module.tar.gz
RUN mkdir -p /usr/src/module \
    && tar -xf /tmp/module.tar.gz -C /usr/src/module --strip-components=1 \
    && rm -f /tmp/module.tar.gz

COPY build.sh /tmp/build.sh
RUN chmod +x /tmp/build.sh && /tmp/build.sh


# runtime: базовый образ + модуль и его библиотеки
FROM ${BASE_IMAGE} AS runtime

COPY --from=build /opt/ext/ /
RUN printf '%s\n' /usr/local/lib > /etc/ld.so.conf.d/nginx-local.conf \
    && ldconfig
