# python
ARG BUILD_IMAGE=dmp/gcc:8.5.0
ARG RUNTIME_IMAGE=dmp/glibc:2.28

FROM ${BUILD_IMAGE} AS builder

ARG PYTHON_VERSION
ARG APT_DEPS
ARG PYTHON_PATH=/opt/python


# build-toolchain (bootstrap-gcc из репо) + dev-зависимости для gcc/python
RUN apt-get update      \
    && apt-get install -y --no-install-recommends \
        ${APT_DEPS}     \
    && apt-get clean    \
    && rm -rf           \
        /var/lib/apt/lists/* \
        /var/cache/apt/archives/*

# pip.conf
COPY pip.conf* /tmp/pipconf/
RUN if [ -f /tmp/pipconf/pip.conf ]; then \
        cp /tmp/pipconf/pip.conf /etc/pip.conf; \
    fi \
    && rm -rf /tmp/pipconf

# Python
COPY artifacts/src/Python-${PYTHON_VERSION}.tar.xz /tmp/Python.tar.xz
RUN cd /tmp \
    && tar -xf Python.tar.xz                \
    && cd Python-${PYTHON_VERSION}          \
    && ./configure                          \
        --enable-optimizations --with-lto   \
        --enable-shared                     \
        --with-ensurepip=install            \
        LDFLAGS=-Wl,-rpath,/usr/local/lib   \
    && make -j"$(nproc)"                    \
    && make install DESTDIR=${PYTHON_PATH}  \
    && ln -sf python3 ${PYTHON_PATH}/usr/local/bin/python \
    && ln -sf python3-config ${PYTHON_PATH}/usr/local/bin/python-config \
    && ln -sf pip3 ${PYTHON_PATH}/usr/local/bin/pip \
    && rm -rf /tmp/*

# Делаю так, что бы поиск пакетов происходил как в /usr/local/lib/pythonX.Y/site-packages
# так и в /usr/lib/pythonX.Y/site-packages
RUN set -eux; \
    SP="$(find ${PYTHON_PATH}/usr/local/lib -maxdepth 2 -type d -name site-packages | head -1)"; \
    PYDIR="$(basename "$(dirname "$SP")")"; \
    printf "import site; site.addsitedir('/usr/lib/%s/site-packages')\n" "$PYDIR" > "$SP/000-usr-fhs.pth"; \
    cat "$SP/000-usr-fhs.pth"


# dist: только собранный /opt/python без сборочного тулчейна — компактный «дистрибутив»
FROM ${RUNTIME_IMAGE} AS dist
ARG PYTHON_PATH=/opt/python
COPY --from=builder ${PYTHON_PATH} ${PYTHON_PATH}


FROM ${RUNTIME_IMAGE} AS runtime

COPY --from=dist /opt/python /

RUN ldconfig

ENTRYPOINT [ "/usr/local/bin/python3" ]
