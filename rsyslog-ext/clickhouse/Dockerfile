# rsyslog-ext/clickhouse: выходной модуль omclickhouse для rsyslog
ARG BUILD_IMAGE=dmp/gcc:14.2.0-dist
ARG RUNTIME_IMAGE=dmp/glibc:2.28

# runtime берёт дистрибутив из готового -dist образа (--build-arg DIST_IMAGE),
# иначе стадия dist компилировалась бы заново
ARG DIST_IMAGE=scratch

FROM ${BUILD_IMAGE} AS dist

ARG RSYSLOG_VERSION
ARG LIBFASTJSON_VERSION
ARG CURL_VERSION
ARG APT_DEPS
# EXT_PATH — только модуль, это его и ставят на целевую машину
# RSYSLOG_PATH — эталонный rsyslog той же версии, нужен для make test
ARG EXT_PATH=/opt/rsyslog-ext
ARG RSYSLOG_PATH=/opt/rsyslog
# каталог модулей rsyslog на целевой машине (RED OS 8 — /usr/lib64/rsyslog)
ARG MODULE_DIR=/usr/lib64/rsyslog

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

# зависимости сборки берутся из штатного репозитория astra: libestr, zlib и
# openssl там подходящие. apt идёт через прокси nexus (sources.list подкладывается
# на машине сборки), остальным каналам в сеть взяться неоткуда — их качает make sources
RUN if [ -n "${APT_DEPS}" ]; then                                         \
        apt-get update                                                    \
        && apt-get install -y --no-install-recommends ${APT_DEPS}         \
        && apt-get clean                                                  \
        && rm -rf /var/lib/apt/lists/* /var/cache/apt/archives/*;         \
    fi

ENV CC=/usr/local/bin/gcc CXX=/usr/local/bin/g++
ENV PKG_CONFIG_PATH=/usr/local/lib/pkgconfig

COPY artifacts/src/ /tmp/src/

# libfastjson: в astra 0.99.8, а rsyslog 8.2602 требует >= 0.99.9 и на 0.99.8
# останавливает configure. Ставится в /usr/local — этот путь libtool считает
# системным (он в ld.so.conf), поэтому rpath в модуль не попадёт
RUN set -eux;                                                             \
    cd /tmp;                                                              \
    tar -xf src/libfastjson-${LIBFASTJSON_VERSION}.tar.gz;                \
    cd libfastjson-${LIBFASTJSON_VERSION};                                \
    ./configure --prefix=/usr/local;                                      \
    make -j"$(nproc)";                                                    \
    make install;                                                         \
    ldconfig;                                                             \
    cd /; rm -rf /tmp/libfastjson-${LIBFASTJSON_VERSION}

# libcurl: в astra он собран с версионированием символов (CURL_OPENSSL_3), а в
# libcurl.so.4 из RED OS такой версии нет — модуль, слинкованный с пакетным
# curl, там просто не загрузится. Ванильный curl версий не проставляет, и
# неверсионированная ссылка ложится и на RED OS, и на debian/astra.
# Библиотека нужна только на линковке: в рантайме берётся системная libcurl.so.4,
# поэтому версия берётся заведомо старая — её символы есть в любой новее.
# openssl и zlib для неё — пакетные (с 8.16 curl требует openssl 3, в astra 1.1.1)
RUN set -eux;                                                             \
    cd /tmp;                                                              \
    tar -xf src/curl-${CURL_VERSION}.tar.gz;                              \
    cd curl-${CURL_VERSION};                                              \
    ./configure --prefix=/usr/local                                       \
        --with-openssl --with-zlib                                        \
        --without-libpsl --without-libidn2 --without-nghttp2              \
        --without-brotli --without-zstd --without-librtmp                 \
        --without-libssh2                                                 \
        --disable-ldap --disable-ldaps --disable-manual --disable-docs;   \
    make -j"$(nproc)";                                                    \
    make install;                                                         \
    ldconfig;                                                             \
    cd /; rm -rf /tmp/curl-${CURL_VERSION}

# rsyslog: собирается целиком и с максимумом модулей. Нужен из него только
# omclickhouse.so — сам плагин лежит в дереве исходников (plugins/omclickhouse),
# апстрим держит его выключенным, и собирать его можно лишь заголовками и
# config.h ровно этой версии: модуль грузится в rsyslogd через dlopen и работает
# с его структурами напрямую. Собранный рядом эталонный rsyslogd тем и полезен,
# что повторяет системный, — на нём make test и проверяет загрузку модуля.
# Выключено только то, для чего в репозитории astra нет библиотеки:
#   mbedtls, wolfssl        libmbedtls / libwolfssl
#   imhttp                  civetweb
#   omazureeventhubs        qpid-proton
#   imhiredis               libevent (пакета нет вовсе; omhiredis собирается)
#   ksi-ls12, ffaup, mmgrok libksi, libfaup, libgrok
#   rfc3195                 liblogging-rfc3195
#   fmhash-xxhash           libxxhash
#   omhdfs                  libhdfs
#   libcap-ng               есть 0.7.7, нужно >= 0.8.2
#   imsolaris               только Solaris
# и fmpcre — он не собирается из-за бага апстрима: в 8.2602.0 макросы
# MODULE_TYPE_FUNCTION/DEF_FMOD_STATIC_DATA стали требовать ';', а файл не
# поправили. Рядом с rsyslog кладутся .so, которых нет в чистом dmp/glibc
RUN set -eux;                                                             \
    cd /tmp;                                                              \
    tar -xf src/rsyslog-${RSYSLOG_VERSION}.tar.gz;                        \
    cd rsyslog-${RSYSLOG_VERSION};                                        \
    ./configure --prefix=/usr/local --disable-dependency-tracking         \
        --enable-clickhouse --enable-elasticsearch --enable-omhttp        \
        --enable-omhttpfs --enable-omotel --enable-mmkubernetes           \
        --enable-imdocker --enable-mmaitag                                \
        --enable-openssl --enable-gnutls --enable-libgcrypt               \
        --enable-imdtls --enable-omdtls --enable-mmrfc5424addhmac         \
        --enable-relp --enable-gssapi-krb5                                \
        --enable-libsystemd --enable-imjournal --enable-omjournal         \
        --enable-mmnormalize --enable-pmnormalize --enable-mmdblookup     \
        --enable-mysql --enable-pgsql --enable-libdbi                     \
        --enable-snmp --enable-mmsnmptrapd                                \
        --enable-imkafka --enable-omkafka --enable-omhiredis              \
        --enable-ommongodb --enable-omrabbitmq                            \
        --enable-imczmq --enable-omczmq                                   \
        --enable-omudpspoof --enable-omtcl --enable-omuxsock              \
        --enable-impstats --enable-impstats-push                          \
        --enable-imfile --enable-imptcp --enable-imdiag --enable-improg   \
        --enable-imbatchreport --enable-imtuxedoulog --enable-impcap      \
        --enable-klog --enable-kmsg --enable-mail --enable-inet           \
        --enable-fmhash --enable-fmhttp --enable-fmunflatten              \
        --enable-mmanon --enable-mmaudit --enable-mmcount --enable-mmfields \
        --enable-mmjsonparse --enable-mmjsontransform --enable-mmleefparse \
        --enable-mmpstrucdata --enable-mmrm1stspace --enable-mmsequence   \
        --enable-mmsnareparse --enable-mmtaghostname --enable-mmutf8fix   \
        --enable-mmdarwin --enable-omfile-hardened --enable-omprog        \
        --enable-omruleset --enable-omsendertrack --enable-omstdout       \
        --enable-pmaixforwardedfrom --enable-pmciscoios                   \
        --enable-pmcisconames --enable-pmdb2diag --enable-pmlastmsg       \
        --enable-pmnull --enable-pmpanngfw --enable-pmsnare               \
        --enable-regexp --enable-uuid --enable-libzstd                    \
        --enable-liblogging-stdlog --enable-jemalloc                      \
        --enable-usertools --enable-diagtools;                            \
    make -j"$(nproc)";                                                    \
    make install DESTDIR=${RSYSLOG_PATH};                                 \
    mkdir -p ${EXT_PATH}${MODULE_DIR};                                    \
    install -m 0755 plugins/omclickhouse/.libs/omclickhouse.so            \
        ${EXT_PATH}${MODULE_DIR}/omclickhouse.so;                         \
    cd /; rm -rf /tmp/rsyslog-${RSYSLOG_VERSION}

# рантайм эталонной сборки: чистый dmp/glibc не знает ни про jemalloc, ни про
# krb5/kafka/mongoc и прочее, чем rsyslog оброс при сборке «по максимуму».
# Список берётся по ldd самого rsyslogd и всех его модулей; из него убирается
# то, что и так есть в базовом образе. cp -aL — ldd отдаёт путь к symlink
RUN set -eux;                                                             \
    dst=${RSYSLOG_PATH}/usr/local/lib;                                    \
    { ldd ${RSYSLOG_PATH}/usr/local/sbin/rsyslogd;                        \
      for so in ${RSYSLOG_PATH}/usr/local/lib/rsyslog/*.so; do ldd $so; done; } \
      | sed -n 's|.*=> \(/[^ ]*\) .*|\1|p' | sort -u > /tmp/libs.txt;     \
    while read -r lib; do                                                 \
        case "$(basename $lib)" in                                        \
            libc.so.*|libm.so.*|libdl.so.*|librt.so.*|libpthread.so.*)  continue ;; \
            libresolv.so.*|libnsl.so.*|libcrypt.so.*|libutil.so.*)      continue ;; \
            ld-linux*|libgcc_s.so.*|libstdc++.so.*)                     continue ;; \
        esac;                                                             \
        cp -aL "$lib" "$dst/";                                            \
    done < /tmp/libs.txt;                                                 \
    ls "$dst"

# модуль поедет к чужому rsyslogd, поэтому связи проверяются здесь же:
# сторонних .so, кроме системной libcurl, быть не должно, rpath на сборочные
# каталоги — тоже, а версии символов GLIBC_ определяют, где он загрузится
ARG GLIBC_MAX=2.17
ARG NEEDED_ALLOWED="libcurl.so.4 libm.so.6 libc.so.6 libpthread.so.0 libdl.so.2 libresolv.so.2 librt.so.1"
RUN set -eux;                                                             \
    so=${EXT_PATH}${MODULE_DIR}/omclickhouse.so;                          \
    ls -la $so;                                                           \
    ldd $so;                                                              \
    readelf -d $so | grep -qE 'RPATH|RUNPATH'                             \
        && { echo "! в модуле остался rpath"; exit 1; } || true;          \
    for lib in $(readelf -d $so | sed -n 's/.*Shared library: \[\(.*\)\]/\1/p'); do \
        case " ${NEEDED_ALLOWED} " in                                     \
            *" $lib "*) ;;                                                \
            *) echo "! модуль тянет постороннюю библиотеку: $lib"; exit 1 ;; \
        esac;                                                             \
    done;                                                                 \
    vers=$(objdump -T $so | sed -n 's/.*\(GLIBC_[0-9.]*\).*/\1/p' | sort -uV); \
    echo "$vers";                                                         \
    max=$(echo "$vers" | sed 's/GLIBC_//' | sort -V | tail -1);           \
    [ "$max" = "$(printf '%s\n%s\n' "$max" "${GLIBC_MAX}" | sort -V | head -1)" ] \
        || { echo "! модулю нужен glibc $max, порог ${GLIBC_MAX}"; exit 1; }

# примеры конфигурации едут рядом с модулем
COPY conf.d/ ${EXT_PATH}/etc/rsyslog.d/


FROM ${DIST_IMAGE} AS distimg

FROM ${RUNTIME_IMAGE} AS runtime

ARG EXT_PATH=/opt/rsyslog-ext
ARG RSYSLOG_PATH=/opt/rsyslog
ARG MODULE_DIR=/usr/lib64/rsyslog

# эталонный rsyslog той же версии — на нём make test проверяет загрузку модуля
COPY --from=distimg ${RSYSLOG_PATH} /
# сам модуль лежит отдельным деревом: его забирает make extract
COPY --from=distimg ${EXT_PATH} ${EXT_PATH}
RUN printf '%s\n'           \
        /usr/local/lib      \
        /usr/local/lib64    \
        > /etc/ld.so.conf.d/rsyslog-local.conf  \
    && ldconfig                                 \
    && mkdir -p /usr/local/lib/rsyslog          \
    && cp ${EXT_PATH}${MODULE_DIR}/omclickhouse.so /usr/local/lib/rsyslog/

ENTRYPOINT [ "/usr/local/sbin/rsyslogd" ]
CMD [ "-n", "-f", "/etc/rsyslog.conf" ]
