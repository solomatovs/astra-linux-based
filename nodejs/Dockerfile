ARG BASE_IMAGE=dmp/glibc:2.28
ARG RUNTIME_IMAGE=dmp/glibc:2.28
ARG PYTHON_IMAGE=dmp/python:3.11-dist
ARG GCC_IMAGE=dmp/gcc:11.5.0-dist
ARG LLVM_IMAGE=dmp/llvm:20.1.8-dist
ARG GCC_PATH=/opt/gcc
ARG LLVM_PATH=/opt/llvm
ARG NODE_PATH=/opt/nodejs

FROM ${PYTHON_IMAGE} AS python
FROM ${GCC_IMAGE} AS gcc
FROM ${LLVM_IMAGE} AS llvm
FROM ${BASE_IMAGE} AS base
FROM base AS dist

ARG NODE_VERSION
ARG NODE_PATH
ARG GCC_PATH
ARG LLVM_PATH

COPY --from=gcc ${GCC_PATH} /
COPY --from=gcc /usr/include/ /usr/include/
COPY --from=llvm ${LLVM_PATH}/usr/local/ /usr/local/
COPY --from=python /opt/python/usr/local/ /usr/local/

RUN ln -sf /usr/local/bin/python3.11 /usr/local/bin/python3 \
    && ldconfig

COPY artifacts/src/node-v${NODE_VERSION}.tar.xz /tmp/node.tar.xz
RUN cd /tmp \
    && tar -xf node.tar.xz \
    && cd node-v${NODE_VERSION} \
    && export CC=clang CXX=clang++ CC_host=clang CXX_host=clang++ \
    && ./configure --prefix=/usr/local \
    && make -j"$(nproc)" \
    && make install DESTDIR=${NODE_PATH} \
    && rm -f ${NODE_PATH}/usr/local/CHANGELOG.md \
          ${NODE_PATH}/usr/local/LICENSE \
          ${NODE_PATH}/usr/local/README.md \
    && cd / \
    && rm -rf /tmp/*


FROM ${RUNTIME_IMAGE} AS runtime

ARG NODE_PATH
ARG GCC_PATH

COPY --from=dist ${NODE_PATH} /

COPY --from=gcc ${GCC_PATH}/usr/local/lib64/libstdc++.so.6* ${GCC_PATH}/usr/local/lib64/libgcc_s.so.1* ${GCC_PATH}/usr/local/lib64/libatomic.so.1* /usr/local/lib64/
RUN printf '%s\n' /usr/local/lib64 /usr/local/lib > /etc/ld.so.conf.d/nodejs-local.conf \
    && ldconfig

COPY artifacts/npmrc* /tmp/npmrc/
RUN if [ -f /tmp/npmrc/npmrc ]; then cp /tmp/npmrc/npmrc /usr/local/etc/npmrc; fi && rm -rf /tmp/npmrc

ENV NODE_EXTRA_CA_CERTS=/etc/ssl/certs/ca-certificates.crt

ENTRYPOINT [ "/usr/local/bin/node" ]
