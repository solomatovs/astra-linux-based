# openvsx
ARG RUNTIME_IMAGE=dmp/java:17
ARG BUILD_IMAGE=dmp/java:17
ARG NODE_IMAGE=dmp/nodejs:22.22.1-dist
ARG GCC_IMAGE=dmp/gcc:12.4.0-dist

# runtime берёт дистрибутив из готового -dist образа (--build-arg DIST_IMAGE),
# иначе стадия builder компилировалась бы заново
ARG DIST_IMAGE=scratch

FROM ${NODE_IMAGE}      AS node
FROM ${GCC_IMAGE}       AS gcc
FROM ${BUILD_IMAGE}     AS base
FROM base AS builder

ARG OVSX_VERSION
ARG OVSX_PATH=/opt/openvsx
ARG SRC=/usr/src/openvsx
ARG NODE_PATH=/opt/nodejs
ARG GCC_PATH=/opt/gcc

# node для сборки webui; его libstdc++ новее системной — берем из того же тулчейна
COPY --from=node ${NODE_PATH}/usr/local/ /usr/local/
# дистрибутивные libstdc++/libgcc_s вытесняются библиотеками из тулчейна
# (старый libstdc++ знает CXXABI только до 1.3.10) — убираем их до копирования
RUN rm -f /usr/lib/x86_64-linux-gnu/libstdc++.so.6* /lib/x86_64-linux-gnu/libgcc_s.so.1
COPY --from=gcc ${GCC_PATH}/usr/lib/x86_64-linux-gnu/libstdc++.so.6* ${GCC_PATH}/usr/lib/x86_64-linux-gnu/libgcc_s.so.1 ${GCC_PATH}/usr/lib/x86_64-linux-gnu/libatomic.so.1* /usr/lib/x86_64-linux-gnu/
RUN ldconfig


# patch — накатывание patches/ на исходники
RUN apt-get update                                              \
    && apt-get install -y --no-install-recommends patch         \
    && apt-get clean                                            \
    && rm -rf /var/lib/apt/lists/* /var/cache/apt/archives/*

# init.gradle (nexus для gradle, включая buildscript-блок openvsx)
COPY artifacts/init.gradle* /tmp/gradleinit/
RUN if [ -f /tmp/gradleinit/init.gradle ]; then \
        cp /tmp/gradleinit/init.gradle /usr/local/gradle/init.d/init.gradle; \
    fi \
    && rm -rf /tmp/gradleinit

COPY artifacts/yarnrc.yml* /tmp/yarnrc/
COPY artifacts/src/openvsx-${OVSX_VERSION}.tar.gz /tmp/src/openvsx.tar.gz
COPY patches/ /tmp/src/patches/

RUN set -eux;                                                                       \
    mkdir -p ${SRC};                                                                \
    tar -xzf /tmp/src/openvsx.tar.gz -C ${SRC} --strip-components=1;                 \
    cd ${SRC};                                                                      \
    for p in /tmp/src/patches/*.diff; do patch -p1 < "$p"; done

# webui: yarn качать не надо, он вендорен в исходниках (webui/.yarn/releases);
# artifacts/yarnrc.yml дописывается в конец .yarnrc.yml (nexus вместо registry).
# обертка в /usr/local/bin нужна скриптам package.json, которые зовут yarn по имени
RUN set -eux;                                                                       \
    cd ${SRC}/webui;                                                                \
    if [ -f /tmp/yarnrc/yarnrc.yml ]; then cat /tmp/yarnrc/yarnrc.yml >> .yarnrc.yml; fi; \
    rm -rf /tmp/yarnrc;                                                             \
    YARN_CJS="$(ls ${SRC}/webui/.yarn/releases/yarn-*.cjs)";                        \
    printf '%s\n' '#!/bin/sh' "exec node $YARN_CJS \"\$@\""                         \
        > /usr/local/bin/yarn;                                                      \
    chmod +x /usr/local/bin/yarn;                                                   \
    yarn install --immutable;                                                       \
    yarn run build;                                                                 \
    yarn run build:default

# собранный webui кладется внутрь jar-ника: Spring Boot раздает classpath:/static
RUN cp -a ${SRC}/webui/static ${SRC}/server/src/main/resources/static

# server: системный gradle вместо wrapper-а — тот качает дистрибутив gradle
# с services.gradle.org; --no-build-cache — remote build cache апстрима
# (develocity-staging.eclipse.org) в закрытом контуре дает только таймауты
RUN set -eux;                                                                       \
    cd ${SRC}/server;                                                               \
    gradle --no-daemon --no-build-cache assemble;                                   \
    ls -la build/libs/

# дистрибутив — распакованный jar, так его запускает и апстрим (run-server.sh)
RUN set -eux;                                                                       \
    mkdir -p ${OVSX_PATH}/server;                                                   \
    cd ${OVSX_PATH}/server;                                                         \
    jar -xf ${SRC}/server/build/libs/openvsx-server.jar;                            \
    cp ${SRC}/server/scripts/run-server.sh .;                                       \
    chmod +x run-server.sh;                                                         \
    du -h --max-depth=1 ${OVSX_PATH}/server


# dist: только собранный /opt/openvsx без исходников и node/gradle-кэшей
FROM base AS dist
ARG OVSX_PATH=/opt/openvsx
COPY --from=builder ${OVSX_PATH} ${OVSX_PATH}


FROM ${DIST_IMAGE} AS distimg

FROM ${RUNTIME_IMAGE} AS runtime

ARG OVSX_VERSION
ARG OVSX_PATH=/opt/openvsx
ARG OVSX_UID=1000
ARG OVSX_GID=1000

# curl — healthcheck сервера
RUN apt-get update                                              \
    && apt-get install -y --no-install-recommends curl          \
    && apt-get clean                                            \
    && rm -rf /var/lib/apt/lists/* /var/cache/apt/archives/*

# Непривилегированный пользователь
RUN groupadd -g ${OVSX_GID} openvsx \
    && useradd -u ${OVSX_UID} -g ${OVSX_GID} -m -d /home/openvsx -s /usr/sbin/nologin openvsx \
    && mkdir -p /home/openvsx/storage \
    && chown -R openvsx:openvsx /home/openvsx

COPY --from=distimg --chown=openvsx:openvsx ${OVSX_PATH}/server /home/openvsx/server

# конфиг по умолчанию; в рантайме переопределяется SPRING_*/OVSX_* переменными
# окружения или монтированием своего файла поверх (см. config/application.yml)
COPY --chown=openvsx:openvsx config/application.yml /home/openvsx/server/config/application.yml
RUN sed -i "s/<OVSX_VERSION>/v${OVSX_VERSION}/g" /home/openvsx/server/config/application.yml

ENV JVM_ARGS=
# .vsix и ассеты при ovsx.storage.local — монтировать том сюда
VOLUME /home/openvsx/storage
EXPOSE 8080

USER openvsx
WORKDIR /home/openvsx/server

ENTRYPOINT [ "./run-server.sh" ]
