# nginx. сторонние модули собираются отдельно, см. ../nginx-ext
ARG BUILD_IMAGE=dmp/gcc:8.5.0-dist
ARG RUNTIME_IMAGE=dmp/glibc:2.28

FROM ${BUILD_IMAGE} AS dist

ARG APT_DEPS
ARG NGINX_PATH=/opt/nginx
# исходники остаются в -dist образе: по ним и по configure.args
# ../nginx-ext собирает динамические модули
ARG NGINX_SRC=/usr/src/nginx
ARG DEPS_SRC=/usr/src/deps

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

ARG NGINX_VERSION
ARG PCRE2_VERSION
ARG ZLIB_VERSION
ARG OPENSSL_VERSION
ARG NGINX_HTTP3=0

COPY artifacts/src/ /tmp/src/
RUN set -eux;                                                            \
    mkdir -p ${DEPS_SRC} $(dirname ${NGINX_SRC});                        \
    tar -xf /tmp/src/pcre2-${PCRE2_VERSION}.tar.gz     -C ${DEPS_SRC};   \
    tar -xf /tmp/src/zlib-${ZLIB_VERSION}.tar.gz       -C ${DEPS_SRC};   \
    tar -xf /tmp/src/openssl-${OPENSSL_VERSION}.tar.gz -C ${DEPS_SRC};   \
    tar -xf /tmp/src/nginx-${NGINX_VERSION}.tar.gz -C $(dirname ${NGINX_SRC}); \
    mv $(dirname ${NGINX_SRC})/nginx-${NGINX_VERSION} ${NGINX_SRC};      \
    cd ${NGINX_SRC};                                                     \
    if [ "${NGINX_HTTP3}" = "1" ]; then HTTP3="--with-http_v3_module"; else HTTP3=""; fi; \
    set --                                                               \
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
        --with-pcre=${DEPS_SRC}/pcre2-${PCRE2_VERSION}                   \
        --with-pcre-jit                                                  \
        --with-zlib=${DEPS_SRC}/zlib-${ZLIB_VERSION}                     \
        --with-openssl=${DEPS_SRC}/openssl-${OPENSSL_VERSION}            \
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
        --with-stream_ssl_preread_module;                                \
    # аргументы configure для сборки модулей в ../nginx-ext
    : > configure.args;                                                  \
    for a in "$@"; do printf '%s\n' "$a" >> configure.args; done;        \
    # обычный бинарник /usr/sbin/nginx
    ./configure "$@";                                                    \
    make -j"$(nproc)";                                                   \
    make install DESTDIR=${NGINX_PATH};                                  \
    # debug-бинарник (--with-debug) /usr/sbin/nginx-debug
    ./configure "$@" --with-debug;                                       \
    make -j"$(nproc)";                                                   \
    cp objs/nginx ${NGINX_PATH}/usr/sbin/nginx-debug;                    \
    strip ${NGINX_PATH}/usr/sbin/nginx ${NGINX_PATH}/usr/sbin/nginx-debug; \
    # каталоги для модулей из ../nginx-ext
    mkdir -p ${NGINX_PATH}/var/cache/nginx ${NGINX_PATH}/var/log/nginx   \
             ${NGINX_PATH}/etc/nginx/modules                             \
             ${NGINX_PATH}/etc/nginx/modules.d;                          \
    # .o не нужны: модулям хватает заголовков и .a
    find ${NGINX_SRC} ${DEPS_SRC} -name '*.o' -delete;                   \
    rm -rf /tmp/src


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
