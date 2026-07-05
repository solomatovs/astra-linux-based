# cmake
ARG BASE_IMAGE=dmp/gcc:11.5.0
ARG RUNTIME_IMAGE=dmp/glibc:2.28

FROM ${BASE_IMAGE} AS base
FROM base AS dist

ARG CMAKE_VERSION
ARG CMAKE_PATH=/opt/cmake

# ./bootstrap собирает временный cmake, которым затем конфигурируется и собирается
# сам cmake. Зависимости (libarchive, zlib, rhash…) идут bundled в исходниках.
COPY artifacts/src/cmake-${CMAKE_VERSION}.tar.gz /tmp/cmake.tar.gz
RUN cd /tmp \
    && tar -xf cmake.tar.gz \
    && cd cmake-${CMAKE_VERSION} \
    && ./bootstrap \
        --parallel="$(nproc)" \
        --prefix=/usr/local \
        --  -DCMAKE_BUILD_TYPE=Release \
            -DCMAKE_USE_OPENSSL=OFF \
    && make -j"$(nproc)" \
    && make install DESTDIR=${CMAKE_PATH} \
    && cd / \
    && rm -rf /tmp/*


FROM ${RUNTIME_IMAGE} AS runtime

ARG CMAKE_PATH=/opt/cmake

COPY --from=dist ${CMAKE_PATH} /

# cmake слинкован с libstdc++/libgcc_s компилятора-сборщика (gcc 11, GLIBCXX_3.4.29),
# в базовом glibc-образе такой libstdc++ нет — берём его из gcc-образа и даём приоритет.
COPY --from=base /usr/local/lib64/libstdc++.so.6* /usr/local/lib64/libgcc_s.so.1* /usr/local/lib64/
RUN printf '%s\n' /usr/local/lib64 /usr/local/lib > /etc/ld.so.conf.d/cmake-local.conf \
    && ldconfig

ENTRYPOINT [ "/usr/local/bin/cmake" ]
