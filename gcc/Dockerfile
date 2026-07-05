# gcc
ARG BASE_IMAGE=dmp/astra-linux:2.12-glibc2.28

FROM ${BASE_IMAGE} AS base
FROM base AS dist

ARG GCC_VERSION
ARG GMP_VERSION
ARG MPFR_VERSION
ARG MPC_VERSION
ARG BINUTILS_VERSION
ARG MAKE_VERSION
ARG GCC_PATH=/opt/gcc

# build-toolchain (bootstrap-gcc из репо) + dev-зависимости для gcc/python
RUN apt-get update \
    && apt-get install -y --no-install-recommends \
        build-essential gawk bison xz-utils gettext \
        zlib1g-dev libncurses5-dev libgdbm-dev libnss3-dev \
        libssl-dev libreadline-dev libffi-dev libsqlite3-dev \
        libbz2-dev liblzma-dev curl \
    && apt-get clean \
    && rm -rf /var/lib/apt/lists/* /var/cache/apt/archives/*

# GCC (libstdc++)
COPY artifacts/src/ /tmp/src/
RUN cd /tmp \
    && tar -xf src/gcc-${GCC_VERSION}.tar.xz \
    && cd gcc-${GCC_VERSION} \
    && tar -xf /tmp/src/gmp-${GMP_VERSION}.tar.xz && mv gmp-${GMP_VERSION} gmp \
    && tar -xf /tmp/src/mpfr-${MPFR_VERSION}.tar.xz && mv mpfr-${MPFR_VERSION} mpfr \
    && tar -xf /tmp/src/mpc-${MPC_VERSION}.tar.gz && mv mpc-${MPC_VERSION} mpc \
    && cd /tmp && mkdir gcc-build && cd gcc-build && \
    ../gcc-${GCC_VERSION}/configure \
        --enable-languages=c,c++ \
        --disable-multilib --disable-bootstrap \
        --disable-libsanitizer --disable-libgomp \
        --disable-libquadmath --disable-libssp \
        --disable-libvtv --disable-libada \
        --with-default-libstdcxx-abi=new \
    && make -j"$(nproc)" \
    && make install-strip DESTDIR=${GCC_PATH} \
    && cd / \
    && rm -rf /tmp/gcc-build /tmp/gcc-${GCC_VERSION}

# binutils
RUN cd /tmp \
    && tar -xf src/binutils-${BINUTILS_VERSION}.tar.xz \
    && mkdir binutils-build && cd binutils-build \
    && ../binutils-${BINUTILS_VERSION}/configure \
        --disable-multilib \
        --disable-nls \
        --disable-werror \
    && make -j"$(nproc)" \
    && make install-strip DESTDIR=${GCC_PATH} \
    && cd / \
    && rm -rf /tmp/binutils-build /tmp/binutils-${BINUTILS_VERSION}

# make
RUN cd /tmp \
    && tar -xf src/make-${MAKE_VERSION}.tar.gz \
    && cd make-${MAKE_VERSION} \
    && ./configure \
        --disable-nls \
        --without-guile \
    && make -j"$(nproc)" \
    && make install-strip DESTDIR=${GCC_PATH} \
    && rm -rf ${GCC_PATH}/usr/local/share \
    && du -h --max-depth=1 ${GCC_PATH} \
    && cd / \
    && rm -rf /tmp/*


FROM base AS runtime

ARG GCC_PATH=/opt/gcc
# устанавливаем GCC 8 в runtime-образ, чтобы можно было собирать C/C++-приложения
COPY --from=dist ${GCC_PATH} /
# копирую заголовки и библиотеки из промежуточного образа, чтобы можно было собирать C/C++-приложения
COPY --from=dist /usr/include/ /usr/include/

# добавляем binutils в PATH, чтобы можно было собирать C/C++-приложения
RUN printf '%s\n' /usr/local/lib64 /usr/local/lib > /etc/ld.so.conf.d/gcc-local.conf \
    && ldconfig

ENTRYPOINT [ "/usr/local/bin/gcc" ]
