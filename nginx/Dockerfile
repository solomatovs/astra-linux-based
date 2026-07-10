# nginx
ARG BUILD_IMAGE=dmp/gcc:8.5.0-dist
ARG RUNTIME_IMAGE=dmp/glibc:2.28

FROM ${BUILD_IMAGE} AS dist

ARG APT_DEPS
ARG NGINX_PATH=/opt/nginx

# gcc
RUN cp -a /opt/gcc/usr/local/. /usr/local/                          \
    && ln -sf gcc /usr/local/bin/cc                                 \
    && ln -sf g++ /usr/local/bin/c++                                \
    && printf '%s\n' /usr/local/lib64 /usr/local/lib > /etc/ld.so.conf.d/gcc-local.conf \
    && ldconfig

# build-toolchain
RUN if [ -n "${APT_DEPS}" ]; then                                   \
        apt-get update                                              \
        && apt-get install -y --no-install-recommends ${APT_DEPS}   \
        && apt-get clean                                            \
        && rm -rf                                                   \
            /var/lib/apt/lists/*                                    \
            /var/cache/apt/archives/*;                              \
    fi

# krb5
ARG KRB5_VERSION
COPY artifacts/src/krb5-${KRB5_VERSION}.tar.gz /tmp/krb5.tar.gz
RUN cd /tmp                             \
    && tar xf krb5.tar.gz               \
    && cd krb5-${KRB5_VERSION}/src      \
    && ./configure                      \
        --prefix=/usr/local             \
        --disable-rpath                 \
        --disable-pkinit                \
    && make -j"$(nproc)"                \
    && make install                     \
    && ldconfig                         \
    && cd / && rm -rf /tmp/*

# OpenLDAP client
ARG OPENLDAP_VERSION
COPY artifacts/src/openldap-${OPENLDAP_VERSION}.tgz /tmp/openldap.tgz
RUN cd /tmp                             \
    && tar xf openldap.tgz              \
    && cd openldap-${OPENLDAP_VERSION}  \
    && ./configure                      \
        --prefix=/usr/local             \
        --disable-slapd --disable-static \
        --without-tls --without-cyrus-sasl --without-systemd \
    && make depend                      \
    && make -j"$(nproc)" -C include     \
    && make -j"$(nproc)" -C libraries   \
    && make -C include install          \
    && make -C libraries install        \
    && ldconfig                         \
    && cd / && rm -rf /tmp/*

ARG NGINX_VERSION
ARG PCRE2_VERSION
ARG ZLIB_VERSION
ARG OPENSSL_VERSION
ARG NGINX_HTTP3=0
ARG LDAP_MODULE_REF
ARG NJS_VERSION

COPY artifacts/src/ /tmp/src/
COPY spnego-http-auth-nginx-module/ /tmp/spnego-http-auth-nginx-module/
RUN set -eux;                                                            \
    cd /tmp;                                                             \
    tar -xf src/pcre2-${PCRE2_VERSION}.tar.gz;                           \
    tar -xf src/zlib-${ZLIB_VERSION}.tar.gz;                             \
    tar -xf src/openssl-${OPENSSL_VERSION}.tar.gz;                       \
    tar -xf src/nginx-${NGINX_VERSION}.tar.gz;                           \
    tar -xf src/nginx-auth-ldap-${LDAP_MODULE_REF}.tar.gz;               \
    tar -xf src/njs-${NJS_VERSION}.tar.gz;                               \
    ( cd njs-${NJS_VERSION}                                              \
      && ./configure --cc=gcc --no-openssl --no-libxml2 --no-zlib        \
                     --no-quickjs --no-pcre                              \
      && make -j"$(nproc)" libnjs );                                     \
    export NJS_OPENSSL=NO NJS_LIBXSLT=NO NJS_ZLIB=NO NJS_QUICKJS=NO;     \
    cd nginx-${NGINX_VERSION};                                           \
    if [ "${NGINX_HTTP3}" = "1" ]; then HTTP3="--with-http_v3_module"; else HTTP3=""; fi; \
    set --                                                               \
        --prefix=/etc/nginx                                             \
        --sbin-path=/usr/sbin/nginx                                      \
        --modules-path=/usr/lib/nginx/modules                            \
        --conf-path=/etc/nginx/nginx.conf                                \
        --error-log-path=/var/log/nginx/error.log                        \
        --http-log-path=/var/log/nginx/access.log                        \
        --pid-path=/run/nginx.pid                                        \
        --lock-path=/run/nginx.lock                                      \
        --http-client-body-temp-path=/var/cache/nginx/client_temp        \
        --http-proxy-temp-path=/var/cache/nginx/proxy_temp               \
        --http-fastcgi-temp-path=/var/cache/nginx/fastcgi_temp           \
        --http-uwsgi-temp-path=/var/cache/nginx/uwsgi_temp               \
        --http-scgi-temp-path=/var/cache/nginx/scgi_temp                 \
        --user=nginx --group=nginx                                       \
        --with-threads                                                   \
        --with-file-aio                                                  \
        --with-compat                                                    \
        --with-pcre=/tmp/pcre2-${PCRE2_VERSION}                          \
        --with-pcre-jit                                                  \
        --with-zlib=/tmp/zlib-${ZLIB_VERSION}                            \
        --with-openssl=/tmp/openssl-${OPENSSL_VERSION}                   \
        --with-openssl-opt=no-tests                                      \
        --with-cc-opt=-I/usr/local/include                               \
        --with-ld-opt="-L/usr/local/lib -Wl,-rpath,/usr/local/lib"       \
        --with-http_ssl_module                                           \
        --with-http_v2_module                                            \
        ${HTTP3}                                                         \
        --with-http_realip_module                                        \
        --with-http_addition_module                                      \
        --with-http_sub_module                                           \
        --with-http_dav_module                                           \
        --with-http_flv_module                                           \
        --with-http_mp4_module                                           \
        --with-http_gunzip_module                                        \
        --with-http_gzip_static_module                                   \
        --with-http_auth_request_module                                  \
        --with-http_random_index_module                                  \
        --with-http_secure_link_module                                   \
        --with-http_degradation_module                                   \
        --with-http_slice_module                                         \
        --with-http_stub_status_module                                   \
        --with-mail                                                      \
        --with-mail_ssl_module                                           \
        --with-stream                                                    \
        --with-stream_ssl_module                                         \
        --with-stream_realip_module                                      \
        --with-stream_ssl_preread_module                                 \
        --add-dynamic-module=/tmp/spnego-http-auth-nginx-module          \
        --add-dynamic-module=/tmp/nginx-auth-ldap-${LDAP_MODULE_REF}     \
        --add-dynamic-module=/tmp/njs-${NJS_VERSION}/nginx;              \
    # обычный бинарник /usr/sbin/nginx
    ./configure "$@";                                                    \
    make -j"$(nproc)";                                                   \
    make install DESTDIR=${NGINX_PATH};                                  \
    # динамические модули обычной сборки /etc/nginx/modules
    mkdir -p ${NGINX_PATH}/etc/nginx/modules;                            \
    cp objs/*.so ${NGINX_PATH}/etc/nginx/modules/;                       \
    # 2) debug-бинарник (--with-debug) /usr/sbin/nginx-debug
    ./configure "$@" --with-debug;                                       \
    make -j"$(nproc)";                                                   \
    cp objs/nginx ${NGINX_PATH}/usr/sbin/nginx-debug;                    \
    # debug-версии модулей -> *.so-debug
    for so in objs/*.so; do cp "$so" "${NGINX_PATH}/etc/nginx/modules/$(basename "$so")-debug"; done; \
    mkdir -p ${NGINX_PATH}/var/cache/nginx ${NGINX_PATH}/var/log/nginx;  \
    strip ${NGINX_PATH}/usr/sbin/nginx ${NGINX_PATH}/usr/sbin/nginx-debug; \
    # рантайм-.so krb5/openldap кладём в DESTDIR (попадут в /usr/local/lib) \
    mkdir -p ${NGINX_PATH}/usr/local/lib;                                \
    for p in libgssapi_krb5 libkrb5 libk5crypto libcom_err               \
             libkrb5support libverto libldap liblber; do                 \
        cp -a /usr/local/lib/$p.so* ${NGINX_PATH}/usr/local/lib/ 2>/dev/null || true; \
    done;                                                                \
    cp -a /usr/local/lib/krb5 ${NGINX_PATH}/usr/local/lib/ 2>/dev/null || true; \
    cd /; rm -rf /tmp/*


FROM ${RUNTIME_IMAGE} AS runtime

ARG NGINX_PATH=/opt/nginx

# устанавливаем собранный nginx (+ krb5/openldap .so) поверх dmp/glibc
COPY --from=dist ${NGINX_PATH} /
RUN printf '%s\n' /usr/local/lib > /etc/ld.so.conf.d/nginx-local.conf \
    && ldconfig

# системный пользователь nginx + логи в stdout/stderr контейнера
RUN groupadd -r nginx --gid=101                                           \
    && useradd -r -g nginx --uid=101 --home-dir=/var/cache/nginx          \
        --shell=/sbin/nologin nginx                                       \
    && mkdir -p /var/cache/nginx /var/log/nginx /run                      \
    && chown -R nginx:nginx /var/cache/nginx /var/log/nginx               \
    && ln -sf /dev/stdout /var/log/nginx/access.log                       \
    && ln -sf /dev/stderr /var/log/nginx/error.log

# nginx.conf replace
COPY nginx.conf* /tmp/nginxconf/
RUN if [ -f /tmp/nginxconf/nginx.conf ]; then                             \
        cp /tmp/nginxconf/nginx.conf /etc/nginx/nginx.conf;               \
    fi                                                                    \
    && rm -rf /tmp/nginxconf

# krb5.conf replace (realm/keytab для SPNEGO)
COPY krb5.conf* /tmp/krb5conf/
RUN if [ -f /tmp/krb5conf/krb5.conf ]; then                               \
        cp /tmp/krb5conf/krb5.conf /etc/krb5.conf;                        \
    fi                                                                    \
    && rm -rf /tmp/krb5conf

EXPOSE 80 443
STOPSIGNAL SIGQUIT

ENTRYPOINT [ "/usr/sbin/nginx" ]
CMD [ "-g", "daemon off;" ]
