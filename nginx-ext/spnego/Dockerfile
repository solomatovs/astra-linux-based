# spnego: Kerberos/SPNEGO-аутентификация (форк с PAC-группами), поверх образа nginx
ARG NGINX_VERSION
ARG BASE_IMAGE=dmp/nginx:${NGINX_VERSION}

# build: собираем модуль в дереве исходников из nginx:*-dist
FROM dmp/nginx:${NGINX_VERSION}-dist AS build

ARG KRB5_VERSION

# krb5 (gssapi): нужен и на сборке, и в рантайме
COPY artifacts/src/krb5-${KRB5_VERSION}.tar.gz /tmp/krb5.tar.gz
RUN set -eux;                               \
    cd /tmp;                                \
    tar xf krb5.tar.gz;                     \
    cd krb5-${KRB5_VERSION}/src;            \
    ./configure                             \
        --prefix=/usr/local                 \
        --disable-rpath                     \
        --disable-pkinit;                   \
    make -j"$(nproc)";                      \
    make install;                           \
    ldconfig;                               \
    cd /; rm -rf /tmp/krb5*

COPY src/ /usr/src/module/
COPY build.sh /tmp/build.sh
RUN chmod +x /tmp/build.sh && /tmp/build.sh


# runtime: базовый образ + модуль и его библиотеки
FROM ${BASE_IMAGE} AS runtime

COPY --from=build /opt/ext/ /
RUN printf '%s\n' /usr/local/lib > /etc/ld.so.conf.d/nginx-local.conf \
    && ldconfig

# krb5.conf replace (realm/keytab для SPNEGO)
COPY krb5.conf* /tmp/krb5conf/
RUN if [ -f /tmp/krb5conf/krb5.conf ]; then                               \
        cp /tmp/krb5conf/krb5.conf /etc/krb5.conf;                        \
    fi                                                                    \
    && rm -rf /tmp/krb5conf
