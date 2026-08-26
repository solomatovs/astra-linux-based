# fluent-bit
ARG BUILD_IMAGE=dmp/gcc:14.2.0-dist
ARG RUNTIME_IMAGE=dmp/glibc:2.28
ARG CMAKE_IMAGE=dmp/cmake:3.27.9-dist

# runtime берёт дистрибутив из готового -dist образа (--build-arg DIST_IMAGE),
# иначе стадия builder компилировалась бы заново
ARG DIST_IMAGE=scratch

FROM ${CMAKE_IMAGE} AS cmake
FROM ${BUILD_IMAGE} AS dist

ARG FLB_VERSION
ARG OPENSSL_VERSION
ARG LIBYAML_VERSION
ARG POSTGRES_VERSION
# On/Off: в 4.0.14 avro ломает конфигурацию cmake (см. DEPS в Makefile)
ARG AVRO_ENCODER=On
ARG CCRONEXPR_VERSION
ARG APT_DEPS
ARG FLB_PATH=/opt/fluent-bit
ARG DEPS_PREFIX=/opt/deps


# cmake из образа dmp/cmake
COPY --from=cmake /opt/cmake /

# gcc-14 toolchain в -dist образе лежит в /opt/gcc — переносим в /usr/local.
# рантайм тулчейна (libstdc++/libgcc_s/libatomic) лежит отдельно, в /usr/lib/x86_64-linux-gnu,
# а в /usr/local/lib64 на него симлинки — без переноса линкер не найдет -lgcc_s.
# дистрибутивные копии убираем до установки: версия должна остаться одна
RUN rm -f /usr/lib/x86_64-linux-gnu/libstdc++.so.6* /lib/x86_64-linux-gnu/libgcc_s.so.1 \
    && cp -a /opt/gcc/usr/local/. /usr/local/                             \
    && cp -a /opt/gcc/usr/lib/x86_64-linux-gnu/. /usr/lib/x86_64-linux-gnu/ \
    && printf '%s\n'                                                      \
        /usr/local/lib64                                                  \
        /usr/local/lib                                                    \
        > /etc/ld.so.conf.d/gcc-local.conf                                \
    && ldconfig

# build-toolchain (flex/bison/m4 — парсеры fluent-bit, pkg-config — поиск зависимостей)
# apt идёт через прокси nexus (sources.list подкладывается на машине сборки)
RUN if [ -n "${APT_DEPS}" ]; then                                         \
        apt-get update                                                    \
        && apt-get install -y --no-install-recommends ${APT_DEPS}         \
        && apt-get clean                                                  \
        && rm -rf /var/lib/apt/lists/* /var/cache/apt/archives/*;         \
    fi

ENV CC=/usr/local/bin/gcc CXX=/usr/local/bin/g++

COPY artifacts/src/ /tmp/src/

# openssl (статически, no-shared)
RUN set -eux;                                                             \
    cd /tmp;                                                              \
    tar -xf src/openssl-${OPENSSL_VERSION}.tar.gz;                        \
    cd openssl-${OPENSSL_VERSION};                                        \
    ./Configure linux-x86_64 no-shared no-tests                           \
        --prefix=${DEPS_PREFIX} --openssldir=${DEPS_PREFIX}/ssl;          \
    make -j"$(nproc)";                                                    \
    make install_sw;                                                      \
    cd /; rm -rf /tmp/openssl-${OPENSSL_VERSION}

# libyaml (статически) — fluent-bit его не вендорит, нужен для YAML-конфигов
RUN set -eux;                                                             \
    cd /tmp;                                                              \
    tar -xf src/yaml-${LIBYAML_VERSION}.tar.gz;                           \
    cd yaml-${LIBYAML_VERSION};                                           \
    ./configure --prefix=${DEPS_PREFIX} --disable-shared --enable-static; \
    make -j"$(nproc)";                                                    \
    make install;                                                         \
    cd /; rm -rf /tmp/yaml-${LIBYAML_VERSION}

# libpq (статически) — для out_pgsql и in_pgsql.
# Собирается только цель libpq: полный make -C src/port тянет серверные объекты
# и падает на генерируемых заголовках бэкенда.
# libpq.a сливается с libpgport/libpgcommon в один архив: порядком -l не управляем.
# --with-gssapi — pg_hba требует Kerberos.
RUN set -eux;                                                             \
    cd /tmp;                                                              \
    tar -xf src/postgresql-${POSTGRES_VERSION}.tar.gz;                    \
    cd postgresql-${POSTGRES_VERSION};                                    \
    ./configure --prefix=${DEPS_PREFIX}                                   \
        --without-readline --without-zlib --without-icu                   \
        --without-ldap --without-libxml                                   \
        --with-openssl --with-gssapi                                      \
        CPPFLAGS="-I${DEPS_PREFIX}/include"                               \
        LDFLAGS="-L${DEPS_PREFIX}/lib -L${DEPS_PREFIX}/lib64";            \
    make -C src/interfaces/libpq -j"$(nproc)";                            \
    make -C src/interfaces/libpq install;                                 \
    make -C src/include install;                                          \
    cd ${DEPS_PREFIX}/lib;                                                \
    rm -f libpq.so*;                                                      \
    printf '%s\n'                                                         \
        'create libpq_all.a'                                              \
        'addlib libpq.a'                                                  \
        "addlib /tmp/postgresql-${POSTGRES_VERSION}/src/common/libpgcommon_shlib.a" \
        "addlib /tmp/postgresql-${POSTGRES_VERSION}/src/port/libpgport_shlib.a"     \
        'save' 'end' | ar -M;                                             \
    mv libpq_all.a libpq.a;                                               \
    ranlib libpq.a;                                                       \
    cd /; rm -rf /tmp/postgresql-${POSTGRES_VERSION}

# fluent-bit: FLB_ALL — максимум плагинов, openssl/libyaml/libpq/libstdc++ линкуются статически
RUN set -eux;                                                           \
    cd /tmp;                                                            \
    tar -xf src/fluent-bit-${FLB_VERSION}.tar.gz;                       \
    cmake -S fluent-bit-${FLB_VERSION} -B build                         \
        -DCMAKE_C_COMPILER=/usr/local/bin/gcc                           \
        -DCMAKE_CXX_COMPILER=/usr/local/bin/g++                         \
        -DCMAKE_C_FLAGS="-D_GNU_SOURCE"                                 \
        -DCMAKE_INSTALL_PREFIX=/usr/local                               \
        -DFLB_RELEASE=On                                                \
        -DFLB_ALL=On                                                    \
        -DFLB_JEMALLOC=On                                               \
        -DFLB_TLS=On                                                    \
        -DFLB_KAFKA=On                                                  \
        -DFLB_HTTP_SERVER=On                                            \
        -DFLB_LUAJIT=On                                                 \
        -DFLB_WASM=On                                                   \
        -DFLB_STREAM_PROCESSOR=On                                       \
        -DFLB_RECORD_ACCESSOR=On                                        \
        -DFLB_SIGNV4=On                                                 \
        -DFLB_METRICS=On                                                \
        -DFLB_CONFIG_YAML=On                                            \
        -DFLB_SHARED_LIB=Off                                            \
        -DFLB_EXAMPLES=Off                                              \
        -DFLB_TESTS_INTERNAL=Off                                        \
        -DFLB_TESTS_RUNTIME=Off                                         \
        -DFLB_OUT_PGSQL=On                                              \
        -DPostgreSQL_ROOT=${DEPS_PREFIX}                                \
        -DPostgreSQL_INCLUDE_DIR=${DEPS_PREFIX}/include                 \
        -DPostgreSQL_TYPE_INCLUDE_DIR=${DEPS_PREFIX}/include            \
        -DPostgreSQL_LIBRARY=${DEPS_PREFIX}/lib/libpq.a                 \
        -DCMAKE_C_STANDARD_LIBRARIES="-lgssapi_krb5"                    \
        -DCMAKE_CXX_STANDARD_LIBRARIES="-lgssapi_krb5"                  \
        -DFLB_IN_SYSTEMD=On                                             \
        -DFLB_IN_UNIX_SOCKET=On                                         \
        -DFLB_IN_EVENT_TEST=On                                          \
        -DFLB_OUT_RETRY=On                                              \
        -DFLB_AVRO_ENCODER=${AVRO_ENCODER}                              \
        -DOPENSSL_ROOT_DIR=${DEPS_PREFIX}                               \
        -DOPENSSL_USE_STATIC_LIBS=On                                    \
        -DFLB_LIBYAML_DIR=${DEPS_PREFIX}                                \
        -DCMAKE_EXE_LINKER_FLAGS="-static-libgcc -static-libstdc++";    \
    cmake --build build -j"$(nproc)";                                   \
    DESTDIR=${FLB_PATH} cmake --install build;                          \
    strip ${FLB_PATH}/usr/local/bin/fluent-bit || true;                 \
    du -h --max-depth=2 ${FLB_PATH}
COPY plugins/ /tmp/plugins/

# Свои плагины из plugins/ — только как .so: встроенные апстрим собирает
# статически. Флаги берутся у cmake из flags.make родного плагина: расхождение
# флагов даёт не ошибку, а порчу памяти (загрузчик делает memcpy структуры).
# Каталог на плагин; plugins/include — общие заголовки, .so из него не делается.
# Рядом компилируется ccronexpr (разбор cron-выражений для Schedule): это не
# библиотека в системе, а один .c, поэтому он просто добавляется в каждый .so.
# Собранное перечисляется в штатном plugins.conf.
RUN set -eux;                                                           \
    cd /tmp;                                                            \
    flags="build/plugins/in_exec/CMakeFiles/flb-plugin-in_exec.dir/flags.make"; \
    defs=$(sed -n 's/^C_DEFINES = //p' $flags);                         \
    incs=$(sed -n 's/^C_INCLUDES = //p' $flags);                        \
    cron=src/ccronexpr-${CCRONEXPR_VERSION};                            \
    ls $cron/ccronexpr.c >/dev/null;                                    \
    mkdir -p ${FLB_PATH}/usr/local/lib/fluent-bit/plugins;              \
    for dir in plugins/*/; do                                           \
        name=$(basename $dir);                                          \
        ls $dir*.c >/dev/null 2>&1 || continue;                         \
        /usr/local/bin/gcc -fPIC -shared -O2 -I plugins/include         \
            -I $cron -DCRON_USE_LOCAL_TIME                              \
            -o ${FLB_PATH}/usr/local/lib/fluent-bit/plugins/flb-$name.so \
            $dir/*.c $cron/ccronexpr.c $defs $incs;                     \
        echo "плагин собран: flb-$name.so";                             \
    done;                                                               \
    {                                                                   \
        echo '[PLUGINS]';                                               \
        for so in ${FLB_PATH}/usr/local/lib/fluent-bit/plugins/*.so; do  \
            echo "    Path /usr/local/lib/fluent-bit/plugins/$(basename $so)"; \
        done;                                                           \
    } > ${FLB_PATH}/usr/local/etc/fluent-bit/plugins.conf;              \
    cat ${FLB_PATH}/usr/local/etc/fluent-bit/plugins.conf;              \
    cd /; rm -rf /tmp/*

# Запросы и примеры конфигураций едут в образе: плагин без запроса бесполезен,
# а держать их в проекте потребителя — значит расходиться с версией плагина
COPY queries/  ${FLB_PATH}/usr/local/share/fluent-bit/queries/
COPY examples/ ${FLB_PATH}/usr/local/share/fluent-bit/examples/


FROM ${DIST_IMAGE} AS distimg

FROM ${RUNTIME_IMAGE} AS runtime

ARG FLB_PATH=/opt/fluent-bit

# устанавливаем собранный fluent-bit поверх чистого dmp/glibc
COPY --from=distimg ${FLB_PATH} /
RUN printf '%s\n'           \
        /usr/local/lib      \
        /usr/local/lib64    \
        > /etc/ld.so.conf.d/fluent-bit-local.conf \
    && ldconfig

# http-сервер мониторинга — 2020, forward — 24224
EXPOSE 2020 24224
STOPSIGNAL SIGTERM

ENTRYPOINT [ "/usr/local/bin/fluent-bit" ]
CMD [ "-c", "/usr/local/etc/fluent-bit/fluent-bit.conf" ]
