# python
ARG BASE_IMAGE=dmp/glibc:2.28
ARG BUILD_IMAGE=dmp/gcc:8.5.0

FROM ${BASE_IMAGE} AS base
FROM ${BUILD_IMAGE} AS dist
ARG PYTHON_VERSION
ARG PYTHON_VERSION_SHORT
ARG APT_DEPS
ARG PYTHON_PATH=/opt/python${PYTHON_VERSION_SHORT}

# build-toolchain (bootstrap-gcc из репо) + dev-зависимости для gcc/python
RUN apt-get update \
    && apt-get install -y --no-install-recommends ${APT_DEPS} \
    && rm -rf /var/lib/apt/lists/*

# pip.conf
COPY artifacts/pip.conf* /tmp/pipconf/
RUN if [ -f /tmp/pipconf/pip.conf ]; then cp /tmp/pipconf/pip.conf /etc/pip.conf; fi && rm -rf /tmp/pipconf

# Python
COPY artifacts/src/Python-${PYTHON_VERSION}.tar.xz /tmp/Python.tar.xz
RUN cd /tmp \
    && tar -xf Python.tar.xz \
    && cd Python-${PYTHON_VERSION} && \
    ./configure \
        --enable-optimizations --with-lto \
        --enable-shared \
        --with-ensurepip=install \
        LDFLAGS=-Wl,-rpath,/usr/local/lib \
    && make -j"$(nproc)" \
    && make install DESTDIR=${PYTHON_PATH} \
    && rm -rf /tmp/*

FROM ${BASE_IMAGE} AS runtime

ARG PYTHON_VERSION_SHORT

COPY --from=dist /opt/python${PYTHON_VERSION_SHORT} /

ENTRYPOINT [ "/usr/local/bin/python3" ]
