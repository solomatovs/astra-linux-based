# gcc поверх готовой базы astra-linux:${ASTRA_VERSION}-${GLIBC_VERSION}
ARG BASE_IMAGE=astra-linux:2.12-glibc2.28
ARG BUILD_IMAGE=astra-linux:2.12-glibc2.28-gcc8.5.0

FROM ${BUILD_IMAGE} AS dist
ARG PYTHON_VERSION
ARG PYTHON_VERSION_SHORT
ARG PYTHON_PATH=/opt/python${PYTHON_VERSION_SHORT}

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
    && ls -al ${PYTHON_PATH}/usr/local/bin/python* \
    && ls -al ${PYTHON_PATH}/usr/local/lib/python* \
    && ls -al ${PYTHON_PATH}/usr/local/lib/libpython* \
    && ls -al ${PYTHON_PATH}/usr/local/lib/pkgconfig/python* \
    && ls -al ${PYTHON_PATH}/usr/local/include/python* \
    && ls -al ${PYTHON_PATH}/usr/local \
    && rm -rf /tmp/*


FROM ${BASE_IMAGE} AS runtime

COPY --from=dist ${PYTHON_PATH} /
