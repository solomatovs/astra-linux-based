# php
ARG BUILD_IMAGE=dmp/gcc:8.5.0-dist
ARG RUNTIME_IMAGE=dmp/glibc:2.28

FROM ${BUILD_IMAGE} AS builder

ARG PHP_VERSION
ARG ZLIB_VERSION
ARG CURL_VERSION
ARG LIBPQ_VERSION
ARG ZIP_VERSION
ARG APT_DEPS
ARG PHP_PATH=/opt/php

# gcc-тулчейн из slim gcc:*-dist лежит в /opt/gcc — переносим в /usr/local
RUN cp -a /opt/gcc/usr/local/. /usr/local/                          \
    && ln -sf gcc /usr/local/bin/cc                                 \
    && ln -sf g++ /usr/local/bin/c++                                \
    && printf '%s\n' /usr/local/lib64 /usr/local/lib > /etc/ld.so.conf.d/gcc-local.conf \
    && ldconfig


# build-toolchain
RUN apt-get update                  \
    && apt-get install -y --no-install-recommends \
        ${APT_DEPS}                 \
    && apt-get clean                \
    && rm -rf                       \
        /var/lib/apt/lists/*        \
        /var/cache/apt/archives/*

# zlib compile & install
COPY artifacts/src/zlib-${ZLIB_VERSION}.tar.gz /tmp/zlib.tar.gz
RUN cd /tmp                                                         \
    && tar -xf zlib.tar.gz                                          \
    && cd zlib-${ZLIB_VERSION}                                      \
    && ./configure --prefix=/usr/local                              \
    && make -j"$(nproc)"                                            \
    && make install                                                 \
    && mkdir -p ${PHP_PATH}/usr/local/lib                           \
    && cp -a /usr/local/lib/libz.so* ${PHP_PATH}/usr/local/lib/     \
    && ldconfig                                                     \
    && cd / && rm -rf /tmp/*

# libcurl compile & install
COPY artifacts/src/curl-${CURL_VERSION}.tar.xz /tmp/curl.tar.xz
RUN cd /tmp                                                         \
    && tar -xf curl.tar.xz                                          \
    && cd curl-${CURL_VERSION}                                      \
    && ./configure --prefix=/usr/local                              \
        --with-openssl --with-zlib                                  \
        --without-libpsl --without-libidn2 --without-nghttp2        \
        --without-brotli --without-zstd --without-librtmp           \
        --without-gssapi --without-libssh2                          \
        --disable-ldap --disable-ldaps --disable-manual             \
    && make -j"$(nproc)"                                            \
    && make install                                                 \
    && cp -a /usr/local/lib/libcurl.so* ${PHP_PATH}/usr/local/lib/  \
    && ldconfig                                                     \
    && cd / && rm -rf /tmp/*

# libpq compile & install
COPY artifacts/src/postgresql-${LIBPQ_VERSION}.tar.bz2 /tmp/pg.tar.bz2
RUN cd /tmp                                                         \
    && tar -xf pg.tar.bz2                                           \
    && cd postgresql-${LIBPQ_VERSION}                               \
    && ./configure --prefix=/usr/local                              \
        --without-readline --without-icu --without-zlib --with-openssl \
    && make -C src/port    -j"$(nproc)"                             \
    && make -C src/common  -j"$(nproc)"                             \
    && make -C src/interfaces/libpq -j"$(nproc)"                    \
    && make -C src/interfaces/libpq install                         \
    && make -C src/include install                                  \
    && make -C src/bin/pg_config install                            \
    && cp -a /usr/local/lib/libpq.so* ${PHP_PATH}/usr/local/lib/    \
    && ldconfig                                                     \
    && cd / && rm -rf /tmp/*

# libzip compile & install (через cmake): системный libzip в Astra Orel — 1.1.2,
# а php 8.x требует libzip>=1.3.0. Собираем свежий (zlib+bzip2+openssl) в /usr/local
# и дублируем .so в ${PHP_PATH} для runtime.
COPY artifacts/src/libzip-${ZIP_VERSION}.tar.xz /tmp/libzip.tar.xz
RUN cd /tmp                                                         \
    && tar -xf libzip.tar.xz                                        \
    && cd libzip-${ZIP_VERSION}                                     \
    && mkdir build && cd build                                      \
    && cmake ..                                                     \
        -DCMAKE_INSTALL_PREFIX=/usr/local                           \
        -DCMAKE_INSTALL_LIBDIR=lib                                  \
        -DBUILD_SHARED_LIBS=ON                                      \
        -DENABLE_OPENSSL=ON                                         \
        -DENABLE_GNUTLS=OFF -DENABLE_MBEDTLS=OFF                    \
        -DENABLE_ZSTD=OFF -DENABLE_LZMA=OFF                         \
        -DBUILD_TOOLS=OFF -DBUILD_REGRESS=OFF                       \
        -DBUILD_EXAMPLES=OFF -DBUILD_DOC=OFF                        \
    && make -j"$(nproc)"                                            \
    && make install                                                 \
    && cp -a /usr/local/lib/libzip.so* ${PHP_PATH}/usr/local/lib/   \
    && ldconfig                                                     \
    && cd / && rm -rf /tmp/*

# PHP
# Примечание по argon2: --with-password-argon2 НЕ передаётся (системный libargon2 2016 г. <
# 20161029), НО argon2id полноценно работает — PASSWORD_ARGON2ID/PASSWORD_ARGON2I берутся из
# ext/sodium (--with-sodium). ext/zip собирается против свежего libzip (см. выше).
COPY artifacts/src/php-${PHP_VERSION}.tar.xz /tmp/php.tar.xz
RUN cd /tmp                                         \
    && tar -xf php.tar.xz                           \
    && cd php-${PHP_VERSION}                        \
    && export PKG_CONFIG_PATH=/usr/local/lib/pkgconfig                  \
    && ./configure                                  \
        --prefix=/usr/local                         \
        --with-config-file-path=/usr/local/lib      \
        --with-config-file-scan-dir=/usr/local/lib/php.d                \
        --enable-fpm --with-fpm-user=www-data --with-fpm-group=www-data \
        --enable-mbstring                           \
        --enable-mysqlnd                            \
        --with-mysqli=mysqlnd                       \
        --with-pdo-mysql=mysqlnd                    \
        --with-pgsql --with-pdo-pgsql               \
        --with-openssl                              \
        --with-curl                                 \
        --with-zlib --with-bz2                      \
        --with-zip                                  \
        --enable-bcmath                             \
        --enable-sockets                            \
        --enable-pcntl                              \
        --enable-soap                               \
        --enable-exif                               \
        --enable-ftp                                \
        --enable-gd --with-jpeg --with-freetype     \
        --enable-intl                               \
        --with-sodium                               \
        --with-readline                             \
        --enable-opcache                            \
    && make -j"$(nproc)"                            \
    && make install INSTALL_ROOT=${PHP_PATH}        \
    && mkdir -p ${PHP_PATH}/usr/local/lib/php.d     \
    && ln -sf php ${PHP_PATH}/usr/local/bin/php-cli \
    && cd /                                         \
    && rm -rf /tmp/*


# dist: только собранный /opt/php без сборочного тулчейна — компактный «дистрибутив»
FROM ${RUNTIME_IMAGE} AS dist
ARG PHP_PATH=/opt/php
COPY --from=builder ${PHP_PATH} ${PHP_PATH}


FROM ${RUNTIME_IMAGE} AS runtime

ARG PHP_PATH=/opt/php
ARG RUNTIME_APT_DEPS
ARG COMPOSER_VERSION

RUN if [ -n "${RUNTIME_APT_DEPS}" ]; then                                   \
        apt-get update                                                      \
        && apt-get install -y --no-install-recommends ${RUNTIME_APT_DEPS}   \
        && apt-get clean                                                    \
        && rm -rf /var/lib/apt/lists/* /var/cache/apt/archives/*;           \
    fi

COPY --from=dist ${PHP_PATH} /
RUN ldconfig

# php.ini
COPY artifacts/php.ini* /tmp/phpini/
RUN if [ -f /tmp/phpini/php.ini ]; then                 \
        cp /tmp/phpini/php.ini /usr/local/lib/php.ini;  \
    fi                                                  \
    && rm -rf /tmp/phpini

# composer
COPY artifacts/composer-${COMPOSER_VERSION}.phar /usr/local/bin/composer
RUN chmod +x /usr/local/bin/composer \
    && mkdir -p /usr/local/composer

# corp nexus
COPY artifacts/composer-config.json* /tmp/composerconf/
RUN if [ -f /tmp/composerconf/composer-config.json ]; then                          \
        cp /tmp/composerconf/composer-config.json /usr/local/composer/config.json;  \
    fi                                                                              \
    && rm -rf /tmp/composerconf

ENV COMPOSER_HOME=/usr/local/composer   \
    COMPOSER_ALLOW_SUPERUSER=1          \
    PATH=/usr/local/bin:${PATH}

ENTRYPOINT [ "/usr/local/bin/php" ]
