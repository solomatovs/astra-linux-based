# nginx
ARG BUILD_IMAGE=dmp/gcc:8.5.0-dist
ARG RUNTIME_IMAGE=dmp/glibc:2.28

FROM ${BUILD_IMAGE} AS dist

ARG NGINX_VERSION
ARG PCRE2_VERSION
ARG ZLIB_VERSION
ARG OPENSSL_VERSION
# NGINX_HTTP3: 0 | 1 — http_v3 (QUIC) доступен только с nginx >= 1.25
ARG NGINX_HTTP3=0
ARG APT_DEPS
ARG NGINX_PATH=/opt/nginx

# gcc-тулчейн из slim gcc:*-dist лежит в /opt/gcc — переносим в /usr/local
RUN cp -a /opt/gcc/usr/local/. /usr/local/                          \
    && ln -sf gcc /usr/local/bin/cc                                 \
    && ln -sf g++ /usr/local/bin/c++                                \
    && printf '%s\n' /usr/local/lib64 /usr/local/lib > /etc/ld.so.conf.d/gcc-local.conf \
    && ldconfig

# build-toolchain (perl нужен для OpenSSL Configure)
RUN if [ -n "${APT_DEPS}" ]; then                                   \
        apt-get update                                              \
        && apt-get install -y --no-install-recommends ${APT_DEPS}   \
        && apt-get clean                                            \
        && rm -rf                                                   \
            /var/lib/apt/lists/*                                    \
            /var/cache/apt/archives/*;                              \
    fi

# nginx + pcre2/zlib/openssl линкуются статически (--with-*=<исходники>),
# поэтому runtime-образу не нужны .so-зависимости кроме glibc
COPY artifacts/src/ /tmp/src/
RUN set -eux;                                                            \
    cd /tmp;                                                             \
    tar -xf src/pcre2-${PCRE2_VERSION}.tar.gz;                           \
    tar -xf src/zlib-${ZLIB_VERSION}.tar.gz;                             \
    tar -xf src/openssl-${OPENSSL_VERSION}.tar.gz;                       \
    tar -xf src/nginx-${NGINX_VERSION}.tar.gz;                           \
    cd nginx-${NGINX_VERSION};                                           \
    if [ "${NGINX_HTTP3}" = "1" ]; then HTTP3="--with-http_v3_module"; else HTTP3=""; fi; \
    ./configure                                                          \
        --prefix=/etc/nginx                                              \
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
        --with-stream_ssl_preread_module;                                \
    make -j"$(nproc)";                                                   \
    make install DESTDIR=${NGINX_PATH};                                  \
    mkdir -p ${NGINX_PATH}/var/cache/nginx ${NGINX_PATH}/var/log/nginx;  \
    strip ${NGINX_PATH}/usr/sbin/nginx;                                  \
    cd /; rm -rf /tmp/*


FROM ${RUNTIME_IMAGE} AS runtime

ARG NGINX_PATH=/opt/nginx

# устанавливаем собранный nginx поверх dmp/glibc
COPY --from=dist ${NGINX_PATH} /

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

EXPOSE 80 443
STOPSIGNAL SIGQUIT

ENTRYPOINT [ "/usr/sbin/nginx" ]
CMD [ "-g", "daemon off;" ]
