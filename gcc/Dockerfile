# gcc
ARG RUNTIME_IMAGE=dmp/glibc:2.28
ARG BUILD_IMAGE=dmp/glibc:2.28

# runtime берёт дистрибутив из готового -dist образа (--build-arg DIST_IMAGE),
# иначе стадия builder компилировалась бы заново
ARG DIST_IMAGE=scratch

# системный каталог библиотек. рантайм gcc (libstdc++/libgcc_s/libatomic) кладём
# туда, а не в /usr/local/lib64: иначе в образе будет две версии libstdc++ —
# дистрибутивная и собранная — и какая подхватится, зависит от порядка поиска
ARG SLIBDIR=/usr/lib/x86_64-linux-gnu

FROM ${BUILD_IMAGE} AS prev
ARG GCC_PATH=/opt/gcc

# надо проверить prev образ, существует ли там новый скомпилированный gcc /opt/gcc
# если он есть, то копируем gcc в папку /seed
# так как BUILD_IMAGE может быть двух вариантов:
# 1. базовый glibc образ
# 2. предыдущий gcc образ
RUN mkdir -p /seed \
    && if [ -d "${GCC_PATH}" ] && [ -n "$(ls -A "${GCC_PATH}" 2>/dev/null)" ]; then \
        cp -a "${GCC_PATH}/." /seed/; \
    fi

FROM ${RUNTIME_IMAGE} AS base
FROM base AS builder

ARG GCC_VERSION
ARG GMP_VERSION
ARG MPFR_VERSION
ARG MPC_VERSION
ARG BINUTILS_VERSION
ARG MAKE_VERSION
ARG APT_DEPS
ARG GCC_PATH=/opt/gcc
ARG SLIBDIR


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
    && mkdir -p ${GCC_PATH}${SLIBDIR}               \
    && mv ${GCC_PATH}/usr/local/lib64/libstdc++.so.6*   \
          ${GCC_PATH}/usr/local/lib64/libgcc_s.so.1     \
          ${GCC_PATH}/usr/local/lib64/libatomic.so.1*   \
          ${GCC_PATH}${SLIBDIR}/                    \
    && ln -sf ${SLIBDIR}/libstdc++.so.6 ${GCC_PATH}/usr/local/lib64/libstdc++.so  \
    && ln -sf ${SLIBDIR}/libgcc_s.so.1  ${GCC_PATH}/usr/local/lib64/libgcc_s.so   \
    && ln -sf ${SLIBDIR}/libatomic.so.1 ${GCC_PATH}/usr/local/lib64/libatomic.so  \
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


# dist: только собранный /opt/gcc + /usr/include без сборочного тулчейна — компактный «дистрибутив»
FROM base AS dist
ARG GCC_PATH=/opt/gcc
COPY --from=builder ${GCC_PATH} ${GCC_PATH}
COPY --from=builder /usr/include/ /usr/include/


FROM ${DIST_IMAGE} AS distimg

FROM base AS runtime

ARG GCC_PATH=/opt/gcc
ARG SLIBDIR

# дистрибутивные libstdc++/libgcc_s вытесняются собранными (старый libstdc++ знает
# CXXABI только до 1.3.10), убираем их до установки — версия должна остаться одна
RUN rm -f ${SLIBDIR}/libstdc++.so.6* /lib/x86_64-linux-gnu/libgcc_s.so.1

# устанавливаем собранный gcc в runtime-образ, чтобы собирать C/C++-приложения
COPY --from=distimg ${GCC_PATH} /
# заголовки и библиотеки из промежуточного образа
COPY --from=distimg /usr/include/ /usr/include/

# cc/c++ алиасы + binutils в PATH
RUN ln -sf gcc /usr/local/bin/cc            \
    && ln -sf g++ /usr/local/bin/c++        \
    && printf '%s\n'                        \
        /usr/local/lib64                    \
        /usr/local/lib                      \
        > /etc/ld.so.conf.d/gcc-local.conf  \
    && ldconfig

ENTRYPOINT [ "/usr/local/bin/gcc" ]
