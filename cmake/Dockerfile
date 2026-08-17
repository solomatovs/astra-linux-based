# cmake
ARG BUILD_IMAGE=dmp/gcc:8.5.0
ARG RUNTIME_IMAGE=dmp/glibc:2.28

# runtime берёт дистрибутив из готового -dist образа (--build-arg DIST_IMAGE),
# иначе стадия builder компилировалась бы заново
ARG DIST_IMAGE=scratch

FROM ${BUILD_IMAGE} AS base
FROM base AS builder

ARG CMAKE_VERSION
ARG CMAKE_PATH=/opt/cmake

RUN apt-get update      \
    && apt-get install -y --no-install-recommends \
        libssl-dev      \
    && apt-get clean    \
    && rm -rf /var/lib/apt/lists/*

# ./bootstrap собирает временный cmake, которым затем конфигурируется и собирается
# сам cmake. Зависимости (libarchive, zlib, rhash…) идут bundled в исходниках.
COPY artifacts/src/cmake-${CMAKE_VERSION}.tar.gz /tmp/cmake.tar.gz
RUN cd /tmp                                 \
    && tar -xf cmake.tar.gz                 \
    && cd cmake-${CMAKE_VERSION}            \
    && ./bootstrap                          \
        --parallel="$(nproc)"               \
        --prefix=/usr/local --              \
            -DCMAKE_BUILD_TYPE=Release      \
            -DCMAKE_USE_OPENSSL=ON          \
    && make -j"$(nproc)"                    \
    && make install DESTDIR=${CMAKE_PATH}   \
    && cd /                                 \
    && rm -rf /tmp/*


# dist: только собранный /opt/cmake без тулчейна-сборщика — компактный «дистрибутив»
FROM ${RUNTIME_IMAGE} AS dist
ARG CMAKE_PATH=/opt/cmake
COPY --from=builder ${CMAKE_PATH} ${CMAKE_PATH}


FROM ${DIST_IMAGE} AS distimg

FROM ${RUNTIME_IMAGE} AS runtime

ARG CMAKE_PATH=/opt/cmake

COPY --from=distimg ${CMAKE_PATH} /

# cmake слинкован с libstdc++/libgcc_s компилятора-сборщика (gcc 11, GLIBCXX_3.4.29),
# в базовом glibc-образе такого нет — берём из gcc-образа вместо дистрибутивных
# (старый libstdc++ знает CXXABI только до 1.3.10), поэтому сначала убираем их
RUN rm -f /usr/lib/x86_64-linux-gnu/libstdc++.so.6* /lib/x86_64-linux-gnu/libgcc_s.so.1
COPY --from=base /usr/lib/x86_64-linux-gnu/libstdc++.so.6* /usr/lib/x86_64-linux-gnu/libgcc_s.so.1 /usr/lib/x86_64-linux-gnu/
RUN ldconfig

ENTRYPOINT [ "/usr/local/bin/cmake" ]
