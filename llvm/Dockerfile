# llvm
ARG BASE_IMAGE=dmp/gcc:11.5.0
ARG RUNTIME_IMAGE=dmp/glibc:2.28
ARG CMAKE_IMAGE=dmp/cmake:3.27.9-dist
ARG PYTHON_IMAGE=dmp/python:3.11-dist

FROM ${CMAKE_IMAGE} AS cmake
FROM ${PYTHON_IMAGE} AS python
FROM ${BASE_IMAGE} AS base
FROM base AS dist

ARG LLVM_VERSION
ARG NINJA_VERSION
ARG LLVM_TARGETS=X86
ARG LLVM_PROJECTS=
ARG LLVM_RUNTIMES=
ARG LLVM_PATH=/opt/llvm

# cmake
COPY --from=cmake /opt/cmake/usr/local/ /usr/local/
# python 3.11
COPY --from=python /opt/python/usr/local/ /usr/local/

RUN ln -sf /usr/local/bin/python3.11 /usr/local/bin/python3 \
    && ldconfig

COPY artifacts/src/ /tmp/src/

# ninja
RUN cd /tmp \
    && tar -xf src/ninja-${NINJA_VERSION}.tar.gz \
    && cmake -S ninja-${NINJA_VERSION} -B ninja-build -DCMAKE_BUILD_TYPE=Release \
    && cmake --build ninja-build -j"$(nproc)" \
    && install -m 0755 ninja-build/ninja /usr/local/bin/ninja \
    && rm -rf /tmp/ninja-*

# llvm
RUN cd /tmp \
    && tar -xf src/llvm-project-${LLVM_VERSION}.src.tar.xz \
    && cmake -G Ninja -S llvm-project-${LLVM_VERSION}.src/llvm -B build \
        -DCMAKE_BUILD_TYPE=Release                  \
        -DLLVM_ENABLE_PROJECTS="${LLVM_PROJECTS}"   \
        -DLLVM_ENABLE_RUNTIMES="${LLVM_RUNTIMES}"   \
        -DLLVM_TARGETS_TO_BUILD="${LLVM_TARGETS}"   \
        -DLLVM_DEFAULT_TARGET_TRIPLE=x86_64-pc-linux-gnu \
        -DGCC_INSTALL_PREFIX=/usr/local             \
        -DUSE_DEPRECATED_GCC_INSTALL_PREFIX=ON      \
        -DLLVM_ENABLE_ASSERTIONS=OFF                \
        -DLLVM_ENABLE_ZLIB=OFF                      \
        -DLLVM_ENABLE_LIBXML2=OFF                   \
        -DLLVM_ENABLE_TERMINFO=OFF                  \
        -DLLVM_INCLUDE_TESTS=OFF                    \
        -DLLVM_INCLUDE_EXAMPLES=OFF                 \
        -DLLVM_INCLUDE_BENCHMARKS=OFF               \
        -DLLVM_STATIC_LINK_CXX_STDLIB=ON            \
        -DLLVM_PARALLEL_LINK_JOBS=1                 \
        -DLLDB_ENABLE_PYTHON=OFF                    \
        -DLLDB_ENABLE_LIBEDIT=OFF                   \
        -DLLDB_ENABLE_CURSES=OFF                    \
        -DLLDB_ENABLE_LIBXML2=OFF                   \
        -DPython3_EXECUTABLE=/usr/local/bin/python3.11 \
        -DCMAKE_INSTALL_PREFIX=/usr/local           \
    && cmake --build build -j"$(nproc)"             \
    && DESTDIR=${LLVM_PATH} cmake --install build   \
    && cd / \
    && rm -rf /tmp/*


FROM ${RUNTIME_IMAGE} AS runtime

ARG LLVM_PATH=/opt/llvm

COPY --from=dist ${LLVM_PATH} /
RUN printf '%s\n' \
        /usr/local/lib \
        /usr/local/lib64 \
        /usr/local/lib/x86_64-pc-linux-gnu \
        > /etc/ld.so.conf.d/llvm-local.conf \
    && ldconfig

ENTRYPOINT [ "/usr/local/bin/llvm-config" ]
