# gcc
ARG BASE_IMAGE=dmp/glibc:2.28
ARG BUILDER_IMAGE=dmp/glibc:2.28

FROM ${BUILDER_IMAGE} AS prev
ARG GCC_PATH=/opt/gcc

# надо проверить prev образ, существует ли там новый скомпилированный gcc /opt/gcc
# если он есть, то копируем gcc в папку /seed
# так как BUILDER_IMAGE может быть двух вариантов:
# 1. базовый glibc образ
# 2. предыдущий gcc образ
RUN mkdir -p /seed \
    && if [ -d "${GCC_PATH}" ] && [ -n "$(ls -A "${GCC_PATH}" 2>/dev/null)" ]; then \
        cp -a "${GCC_PATH}/." /seed/; \
    fi

FROM ${BASE_IMAGE} AS base
FROM base AS dist

ARG GCC_VERSION
ARG GMP_VERSION
ARG MPFR_VERSION
ARG MPC_VERSION
ARG BINUTILS_VERSION
ARG MAKE_VERSION
ARG APT_DEPS
ARG GCC_PATH=/opt/gcc


# apt-sources.list replace
COPY apt-sources.list* /tmp/aptsrc/
RUN if [ -f /tmp/aptsrc/apt-sources.list ] && grep -qvE '^[[:space:]]*(#|$)' /tmp/aptsrc/apt-sources.list; then \
        cp /tmp/aptsrc/apt-sources.list /etc/apt/sources.list; \
    fi \
    && rm -rf /tmp/aptsrc

# ca-chain.crt replace
COPY ca-chain.crt* /tmp/cacrt/
RUN if [ -f /tmp/cacrt/ca-chain.crt ] && grep -qE '^-----BEGIN CERTIFICATE-----' /tmp/cacrt/ca-chain.crt; then \
        cp /tmp/cacrt/ca-chain.crt /usr/local/share/ca-certificates/ca-chain.crt \
        && update-ca-certificates; \
    fi \
    && rm -rf /tmp/cacrt


# копируем gcc из предыдущего образа и обновляем ldconfig если он есть в этом образе
# и в зависиомсти от того есть ли gcc новый, то ставим разный набор apt зависимостей
COPY --from=prev /seed/ /
RUN if [ -x /usr/local/bin/gcc ]; then                              \
        apt-get update                                              \
        && apt-get install -y --no-install-recommends               \
            libc6-dev gawk bison flex m4 xz-utils gettext           \
            zlib1g-dev libncurses5-dev curl                         \
        && apt-get clean                                            \
        && rm -rf /var/lib/apt/lists/* /var/cache/apt/archives/*    \
        && printf '%s\n'        \
            /usr/local/lib64    \
            /usr/local/lib      \
            > /etc/ld.so.conf.d/gcc-local.conf \
        && ldconfig \
        ;           \
    else            \
        apt-get update                                              \
        && apt-get install -y --no-install-recommends               \
            build-essential gawk bison xz-utils gettext             \
            zlib1g-dev libncurses5-dev libgdbm-dev libnss3-dev      \
            libssl-dev libreadline-dev libffi-dev libsqlite3-dev    \
            libbz2-dev liblzma-dev curl                             \
        && apt-get clean                                            \
        && rm -rf /var/lib/apt/lists/* /var/cache/apt/archives/*    \
        ;   \
    fi

# GCC (libstdc++)
COPY artifacts/src/ /tmp/src/
RUN cd /tmp                                     \
    && tar -xf src/gcc-${GCC_VERSION}.tar.xz    \
    && cd gcc-${GCC_VERSION}                    \
    && tar -xf /tmp/src/gmp-${GMP_VERSION}.tar.xz && mv gmp-${GMP_VERSION} gmp      \
    && tar -xf /tmp/src/mpfr-${MPFR_VERSION}.tar.xz && mv mpfr-${MPFR_VERSION} mpfr \
    && tar -xf /tmp/src/mpc-${MPC_VERSION}.tar.gz && mv mpc-${MPC_VERSION} mpc      \
    && cd /tmp && mkdir gcc-build && cd gcc-build \
    && if [ -x /usr/local/bin/gcc ]; then export CC=/usr/local/bin/gcc CXX=/usr/local/bin/g++; fi \
    && ../gcc-${GCC_VERSION}/configure              \
        --enable-languages=c,c++                    \
        --disable-multilib --disable-bootstrap      \
        --disable-libsanitizer --disable-libgomp    \
        --disable-libquadmath --disable-libssp      \
        --disable-libvtv --disable-libada           \
        --with-default-libstdcxx-abi=new            \
    && make -j"$(nproc)"                            \
    && make install-strip DESTDIR=${GCC_PATH}       \
    && cd /                                         \
    && rm -rf /tmp/gcc-build /tmp/gcc-${GCC_VERSION}

# binutils
RUN cd /tmp                                             \
    && tar -xf src/binutils-${BINUTILS_VERSION}.tar.xz  \
    && mkdir binutils-build && cd binutils-build        \
    && ../binutils-${BINUTILS_VERSION}/configure        \
        --disable-multilib                              \
        --disable-nls                                   \
        --disable-werror                                \
    && make -j"$(nproc)"                                \
    && make install-strip DESTDIR=${GCC_PATH}           \
    && cd /                                             \
    && rm -rf /tmp/binutils-build /tmp/binutils-${BINUTILS_VERSION}

# make
RUN cd /tmp                                     \
    && tar -xf src/make-${MAKE_VERSION}.tar.gz  \
    && cd make-${MAKE_VERSION}                  \
    && ./configure                              \
        --disable-nls                           \
        --without-guile                         \
    && make -j"$(nproc)"                        \
    && make install-strip DESTDIR=${GCC_PATH}   \
    && rm -rf ${GCC_PATH}/usr/local/share       \
    && du -h --max-depth=1 ${GCC_PATH}          \
    && cd /                                     \
    && rm -rf /tmp/*


FROM base AS runtime

ARG GCC_PATH=/opt/gcc
# устанавливаем собранный gcc в runtime-образ, чтобы собирать C/C++-приложения
COPY --from=dist ${GCC_PATH} /
# заголовки и библиотеки из промежуточного образа
COPY --from=dist /usr/include/ /usr/include/

# cc/c++ алиасы + binutils в PATH
RUN ln -sf gcc /usr/local/bin/cc            \
    && ln -sf g++ /usr/local/bin/c++        \
    && printf '%s\n'                        \
        /usr/local/lib64                    \
        /usr/local/lib                      \
        > /etc/ld.so.conf.d/gcc-local.conf  \
    && ldconfig

ENTRYPOINT [ "/usr/local/bin/gcc" ]
