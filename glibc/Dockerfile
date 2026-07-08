# glibc
ARG BASE_IMAGE=registry.astralinux.ru/library/orel:2.12
ARG PYTHON_IMAGE=registry.astralinux.ru/library/orel:2.12
ARG GCC_IMAGE=registry.astralinux.ru/library/orel:2.12

# надо проверить python образ, существует ли там собранный python /opt/python
# если он есть, то копируем python в папку /seed
FROM ${PYTHON_IMAGE} AS python
ARG PYTHON_PATH=/opt/python
RUN mkdir -p /seed \
    && if [ -d "${PYTHON_PATH}" ] && [ -n "$(ls -A "${PYTHON_PATH}" 2>/dev/null)" ]; then \
        cp -a "${PYTHON_PATH}/." /seed/; \
    fi

# надо проверить gcc образ, существует ли там собранный gcc-тулчейн /opt/gcc
# если он есть, то копируем gcc в папку /seed
FROM ${GCC_IMAGE} AS gcc
ARG GCC_PATH=/opt/gcc
RUN mkdir -p /seed \
    && if [ -d "${GCC_PATH}" ] && [ -n "$(ls -A "${GCC_PATH}" 2>/dev/null)" ]; then \
        cp -a "${GCC_PATH}/." /seed/; \
    fi

FROM ${BASE_IMAGE} AS base

# базовая настройка apt/ca
RUN printf '%s\n'                                                   \
        'APT::Get::AllowUnauthenticated "true";'                    \
        'Acquire::AllowInsecureRepositories "true";'                \
        'Acquire::AllowDowngradeToInsecureRepositories "true";'     \
        > /etc/apt/apt.conf.d/99insecure                            \
    && printf '%s\n'                                                \
        'Dpkg::Options { "--force-confdef"; "--force-confold"; };'  \
        > /etc/apt/apt.conf.d/90noninteractive

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

# ставим локально скачанные .deb пакеты
# это необходимо потому, что apt не сможет стучаться на nexus по https без пакета apt-transport-https
COPY artifacts/apt-bootstrap* /tmp/debs/
RUN if ls /tmp/debs/*.deb >/dev/null 2>&1; then \
        ls -1 /tmp/debs/*.deb \
        && dpkg -i /tmp/debs/*.deb; \
    fi \
    && rm -rf /tmp/debs


FROM base AS dist

ARG GLIBC_VERSION
ARG GLIBC_PATH=/opt/glibc

# копируем gcc из предыдущего образа и обновляем ldconfig если он есть в этом образе
# и в зависиомсти от того есть ли gcc новый, то ставим разный набор apt зависимостей
COPY --from=gcc    /seed/ /
COPY --from=python /seed/ /

RUN if [ -x /usr/local/bin/gcc ]; then                              \
        apt-get update                                              \
        && apt-get install -y --no-install-recommends               \
            gawk bison xz-utils gettext                             \
        && apt-get clean                                            \
        && rm -rf /var/lib/apt/lists/* /var/cache/apt/archives/*    \
        && printf '%s\n'        \
            /usr/local/lib64    \
            /usr/local/lib      \
            > /etc/ld.so.conf.d/gcc-local.conf                      \
        && ldconfig             \
        && gcc --version | head -1 \
        && as  --version | head -1 \
        ;           \
    else            \
        apt-get update                                              \
        && apt-get install -y --no-install-recommends               \
            build-essential gawk bison xz-utils gettext             \
        && apt-get clean                                            \
        && rm -rf /var/lib/apt/lists/* /var/cache/apt/archives/*    \
        ;   \
    fi \
    && if [ -x /usr/local/bin/python3.11 ]; then                    \
        ln -sf /usr/local/bin/python3.11 /usr/local/bin/python3;    \
    fi \
    && ldconfig

# glibc
COPY artifacts/src/glibc-${GLIBC_VERSION}.tar.xz /tmp/
RUN cd /tmp                                     \
    && tar -xf glibc-${GLIBC_VERSION}.tar.xz    \
    && mkdir build && cd build                  \
    && if [ -x /usr/local/bin/gcc ]; then export CC=/usr/local/bin/gcc CXX=/usr/local/bin/g++; fi \
    && ../glibc-${GLIBC_VERSION}/configure      \
        --prefix=/usr                           \
        --libdir=/usr/lib/x86_64-linux-gnu      \
        --disable-sanity-checks --disable-werror\
        --enable-kernel=4.15                    \
        libc_cv_slibdir=/lib/x86_64-linux-gnu   \
        libc_cv_rtlddir=/lib64                  \
    && make -j"$(nproc)"                        \
    && make install PERL=/bin/true DESTDIR=${GLIBC_PATH} \
    && rm -rf   ${GLIBC_PATH}/usr/share/i18n    \
                ${GLIBC_PATH}/usr/share/locale  \
                ${GLIBC_PATH}/usr/share/doc     \
                ${GLIBC_PATH}/usr/share/info    \
    && find ${GLIBC_PATH} -name '*.a' ! -name '*nonshared*' -delete \
    && find ${GLIBC_PATH} -type f -name '*.so*' -exec strip --strip-unneeded {} + 2>/dev/null || true \
    && du -h --max-depth=1 ${GLIBC_PATH}        \
    && cd / && rm -rf /tmp/*


FROM base AS runtime

ARG GLIBC_PATH=/opt/glibc
ARG GLIBC_VERSION
# устанавливаем собранный glibc в runtime-образ
COPY --from=dist ${GLIBC_PATH} /

RUN set -eux \
    && printf '%s\n'        \
        /usr/local/lib      \
        /usr/local/lib64    \
        /lib /usr/lib       \
        > /etc/ld.so.conf.d/000-legacy-libdirs.conf \
    && ldconfig \
    && ldd --version | head -n1

# это лайфхак!
# В astra-linux glibc 2.24, это значит что даже после установки glibc 2.28 из исходников
# если попробовать установить какой-либо пакет из репозитория, то dpkg (или apt)
# перезапишут заголовочные файлы (include файлы) glibc 2.28 на заголовочные файлы glibc 2.24 из репозитория
# решение проблемы такое:
# регистрируем пустой пакет libc6-dev в репозитории dpkg с указанием версии glibc 2.28
# тогда последующие apt install будут видеть, что уже установлена более новая версия glibc
# и не будут перезатирать заголовочные файлы
RUN set -eux \
    && mkdir -p /tmp/fake-libc6-dev/DEBIAN \
    && printf '%s\n'                \
        'Package: libc6-dev'        \
        "Version: ${GLIBC_VERSION}" \
        'Architecture: amd64'       \
        'Maintainer: local'         \
        'Provides: libc-dev'        \
        'Multi-Arch: same'          \
        "Description: dummy libc6-dev protecting source-built glibc ${GLIBC_VERSION} headers" \
        > /tmp/fake-libc6-dev/DEBIAN/control                    \
    && dpkg-deb --build /tmp/fake-libc6-dev /tmp/libc6-dev.deb  \
    && dpkg -i /tmp/libc6-dev.deb   \
    && apt-mark hold libc6-dev      \
    && rm -rf /tmp/fake-libc6-dev /tmp/libc6-dev.deb

# linux-libc-dev (kernel-заголовки: linux/*, asm/*)
# раньше приходил транзитивно через настоящий libc6-dev
# fake-пакет выше его перекрывает, поэтому нужно поставить его явно
RUN apt-get update \
    && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
        locales         \
        linux-libc-dev  \
    && rm -rf /var/lib/apt/lists/*                                      \
    && mkdir -p /usr/lib/x86_64-linux-gnu/locale                        \
    && localedef -i ru_RU -c -f UTF-8 ru_RU.UTF-8                       \
    && locale -a | grep -q '^ru_RU\.utf8$'

ENV LANG=ru_RU.UTF-8        \
    LC_ALL=ru_RU.UTF-8      \
    LANGUAGE=ru_RU.UTF-8
