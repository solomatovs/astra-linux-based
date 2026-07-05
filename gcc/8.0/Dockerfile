# gcc поверх готовой базы astra-linux:${ASTRA_VERSION}-${GLIBC_VERSION}
ARG ASTRA_VERSION=2.12
ARG GLIBC_VERSION=2.28

FROM astra-linux:${ASTRA_VERSION}-glibc${GLIBC_VERSION} AS base
FROM base AS gcc_dist

ARG GCC_VERSION
ARG GMP_VERSION
ARG MPFR_VERSION
ARG MPC_VERSION
ARG GCC_PATH=/opt/gcc

# build-toolchain (bootstrap-gcc из репо) + dev-зависимости для gcc/python
RUN apt-get update \
    && apt-get install -y --no-install-recommends \
        build-essential gawk bison xz-utils gettext \
        zlib1g-dev libncurses5-dev libgdbm-dev libnss3-dev \
        libssl-dev libreadline-dev libffi-dev libsqlite3-dev \
        libbz2-dev liblzma-dev curl \
    && rm -rf /var/lib/apt/lists/*

# libc6-dev (от build-essential) перезаписал заголовки glibc на 2.24 —
# возвращаем ${GLIBC_VERSION} из базы и перелинковываем arch-заголовки multiarch на них,
# иначе сборка против 2.24-заголовков + ${GLIBC_VERSION}-libc даёт implicit-declaration
COPY --from=base /usr/include/ /usr/include/
RUN set -eux \
    && cd /usr/include/x86_64-linux-gnu \
    && dpkg -L libc6-dev | grep -E '^/usr/include/x86_64-linux-gnu/[^/]+$' | while read -r p; do \
        n=${p#/usr/include/x86_64-linux-gnu/}; \
        rm -rf "$n"; ln -s "../$n" "$n"; \
    done

# GCC 8 (libstdc++)
COPY artifacts/gcc-src/ /tmp/gcc-src/
RUN cd /tmp \
    && tar -xf gcc-src/gcc-${GCC_VERSION}.tar.xz \
    && cd gcc-${GCC_VERSION} \
    && tar -xf /tmp/gcc-src/gmp-${GMP_VERSION}.tar.xz && mv gmp-${GMP_VERSION} gmp \
    && tar -xf /tmp/gcc-src/mpfr-${MPFR_VERSION}.tar.xz && mv mpfr-${MPFR_VERSION} mpfr \
    && tar -xf /tmp/gcc-src/mpc-${MPC_VERSION}.tar.gz && mv mpc-${MPC_VERSION} mpc \
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
    && rm -rf ${GCC_PATH}/opt/gcc/share \
    && du -h --max-depth=1 ${GCC_PATH} \
    && cd / \
    && rm -rf /tmp/*


FROM base AS runtime

ARG GCC_PATH=/opt/gcc
# устанавливаем GCC 8 в runtime-образ, чтобы можно было собирать C/C++-приложения
COPY --from=gcc_dist ${GCC_PATH} /
