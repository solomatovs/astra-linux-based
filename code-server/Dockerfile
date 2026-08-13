ARG NODE_IMAGE=dmp/nodejs:22.22.1-dist
ARG RUNTIME_IMAGE=dmp/glibc:2.28
ARG GCC_IMAGE=dmp/gcc:12.4.0-dist
ARG PYTHON_IMAGE=dmp/python:3.10-dist
ARG BUILDX_IMAGE=dmp/docker-buildx:0.30.0-dist
ARG COMPOSE_IMAGE=dmp/docker-compose:2.38.2-dist

FROM ${GCC_IMAGE}       AS gcc
FROM ${PYTHON_IMAGE}    AS python
FROM ${BUILDX_IMAGE}    AS buildx
FROM ${COMPOSE_IMAGE}   AS compose
FROM ${NODE_IMAGE}      AS builder

ARG CS_VERSION
ARG CS_PATH=/opt/code-server
ARG VSCODE_SHA
ARG RIPGREP_ARCHIVE
ARG ELECTRON_TARGET
ARG NODE_TARGET
ARG SRC=/usr/src/code-server
ARG GCC_PATH=/opt/gcc
ARG PYTHON_PATH=/opt/python

RUN cp -a /opt/nodejs/usr/local/. /usr/local/

COPY --from=gcc ${GCC_PATH}/usr/local/ /usr/local/
COPY --from=gcc /usr/include/ /usr/include/
COPY --from=python ${PYTHON_PATH}/usr/local/ /usr/local/
COPY npmrc* /tmp/npmrc/
COPY artifacts/src/code-server-${CS_VERSION}-src.tar.gz         /tmp/src/code-server.tar.gz
COPY artifacts/src/vscode-${VSCODE_SHA}.tar.gz                  /tmp/src/vscode.tar.gz
COPY artifacts/src/${RIPGREP_ARCHIVE}                           /tmp/src/ripgrep.tar.gz
COPY artifacts/src/electron-v${ELECTRON_TARGET}-headers.tar.gz  /tmp/src/electron-headers.tar.gz
COPY artifacts/src/builtin-${CS_VERSION}/                       /tmp/src/vsix/
COPY artifacts/src/gitdeps-${CS_VERSION}/                       /tmp/src/gitdeps/
COPY artifacts/src/agent-browser-${CS_VERSION}/                 /tmp/src/agent-browser/
COPY gitdeps-rewrite.py /usr/local/share/gitdeps-rewrite.py

ENV ELECTRON_SKIP_BINARY_DOWNLOAD=1 PLAYWRIGHT_SKIP_BROWSER_DOWNLOAD=1

RUN set -eux                                                                            \
    && ln -sf gcc /usr/local/bin/cc                                                     \
    && ln -sf g++ /usr/local/bin/c++                                                    \
    && printf '%s\n' /usr/local/lib64 /usr/local/lib > /etc/ld.so.conf.d/gcc-local.conf \
    && ldconfig                                                                         \
    && if [ -f /tmp/npmrc/npmrc ]; then cp /tmp/npmrc/npmrc /usr/local/etc/npmrc; fi    \
    && rm -rf /tmp/npmrc                                                                \
    && apt-get update                                                                   \
    && apt-get install -y --no-install-recommends                                       \
        git quilt jq rsync unzip pkg-config                                             \
        libkrb5-dev libx11-dev libxkbfile-dev libsecret-1-dev                           \
    && apt-get clean                                                                    \
    && rm -rf                                                                           \
        /var/lib/apt/lists/*                                                            \
        /var/cache/apt/archives/*


RUN set -eux                                                                     \
    && mkdir -p ${SRC}                                                           \
    && tar -xzf /tmp/src/code-server.tar.gz -C ${SRC} --strip-components=1       \
    && tar -xzf /tmp/src/vscode.tar.gz -C ${SRC}/lib/vscode --strip-components=1 \
    && git config --global user.email builder@localhost                          \
    && git config --global user.name builder                                     \
    && git config --global init.defaultBranch main


RUN set -eux                                                                          \
    && mkdir -p /usr/src/gitdeps                                                      \
    && for t in /tmp/src/gitdeps/*.tar.gz; do tar -xzf "$t" -C /usr/src/gitdeps; done \
    && python3 /usr/local/share/gitdeps-rewrite.py ${SRC} /usr/src/gitdeps            \
    && cd ${SRC}/lib/vscode                                                           \
    && git init -q                                                                    \
    && git add -A                                                                     \
    && git commit -qm ${VSCODE_SHA}                                                   \
    && cd ${SRC}                                                                      \
    && git init -q                                                                    \
    && git add -A                                                                     \
    && git commit -qm ${CS_VERSION}


RUN set -eux                               \
    && cd ${SRC}                           \
    && QUILT_PATCHES=patches quilt push -a \
    && cd lib/vscode                       \
    && git add -A                          \
    && git commit -qm patched


RUN set -eux                                                                                                                       \
    && INSTALL_VERSION="$(node -p 'require("/usr/local/lib/node_modules/npm/node_modules/node-gyp/package.json").installVersion')" \
    && mkdir -p /root/.cache/node-gyp/${NODE_TARGET}/include                                                                       \
    && cp -a /usr/local/include/node /root/.cache/node-gyp/${NODE_TARGET}/include/                                                 \
    && echo "$INSTALL_VERSION" > /root/.cache/node-gyp/${NODE_TARGET}/installVersion                                               \
    && mkdir -p /root/.cache/node-gyp/${ELECTRON_TARGET}                                                                           \
    && tar -xzf /tmp/src/electron-headers.tar.gz -C /root/.cache/node-gyp/${ELECTRON_TARGET} --strip-components=1                  \
    && echo "$INSTALL_VERSION" > /root/.cache/node-gyp/${ELECTRON_TARGET}/installVersion                                           \
    && mkdir -p ${SRC}/lib/vscode/.build/node/v${NODE_TARGET}/linux-x64                                                            \
    && cp /opt/nodejs/usr/local/bin/node ${SRC}/lib/vscode/.build/node/v${NODE_TARGET}/linux-x64/node                              \
    && for vsix in /tmp/src/vsix/*.vsix; do                                                                                        \
        name="$(basename "$vsix" .vsix)"; name="${name%-*}";                                                                       \
        mkdir -p ${SRC}/lib/vscode/.build/builtInExtensions/$name                                                                  \
        && unzip -q "$vsix" 'extension/*' -d /tmp/vsix-unpack                                                                      \
        && mv /tmp/vsix-unpack/extension/* ${SRC}/lib/vscode/.build/builtInExtensions/$name/                                       \
        && rm -rf /tmp/vsix-unpack;                                                                                                \
    done


RUN set -eux                                                  \
    && export npm_config_ignore_scripts=true                  \
    && cd ${SRC} && npm --loglevel verbose install            \
    && cd ${SRC}/lib/vscode && npm --loglevel verbose install \
    && node build/npm/postinstall.ts


RUN set -eux                                                                                                   \
    && mkdir -p /tmp/rg                                                                                        \
    && tar -xzf /tmp/src/ripgrep.tar.gz -C /tmp/rg rg                                                          \
    && find ${SRC} -type d -path '*/node_modules/@vscode/ripgrep'                                              \
        -exec sh -c 'mkdir -p "$1/bin" && cp /tmp/rg/rg "$1/bin/rg"' _ {} \;                                   \
    && if [ -d ${SRC}/lib/vscode/node_modules/agent-browser ]; then                                            \
        mkdir -p ${SRC}/lib/vscode/node_modules/agent-browser/bin                                              \
        && cp /tmp/src/agent-browser/agent-browser-linux-x64 ${SRC}/lib/vscode/node_modules/agent-browser/bin/ \
        && chmod 755 ${SRC}/lib/vscode/node_modules/agent-browser/bin/agent-browser-linux-x64;                 \
    fi                                                                                                         \
    && ${SRC}/lib/vscode/node_modules/@vscode/ripgrep/bin/rg --version                                         \
    && for d in ${SRC} ${SRC}/lib/vscode ${SRC}/lib/vscode/build                                               \
             ${SRC}/lib/vscode/remote ${SRC}/lib/vscode/extensions; do                                         \
        if [ -d "$d/node_modules" ]; then                                                                      \
            cd "$d" && npm rebuild --unsafe-perm;                                                              \
        fi;                                                                                                    \
    done


RUN set -eux     \
    && cd ${SRC} \
    && npm run build


RUN set -eux                        \
    && cd ${SRC}                    \
    && export VERSION=${CS_VERSION} \
    && npm run build:vscode


RUN set -eux                                                                          \
    && cd ${SRC}                                                                      \
    && KEEP_MODULES=1 npm run release                                                 \
    && mkdir -p ${CS_PATH}/usr/local/lib ${CS_PATH}/usr/local/bin                     \
    && cp -a ${SRC}/release ${CS_PATH}/usr/local/lib/code-server                      \
    && ln -sf ../lib/code-server/bin/code-server ${CS_PATH}/usr/local/bin/code-server \
    && find ${CS_PATH} -type d \( -name obj -o -name obj.target -o -name .deps \)     \
        -prune -exec rm -rf {} +                                                      \
    && find ${CS_PATH} -type f \( -name '*.o' -o -name '*.a' \) -delete               \
    && du -h --max-depth=1 ${CS_PATH}/usr/local/lib/code-server



FROM ${RUNTIME_IMAGE} AS dist
ARG CS_PATH=/opt/code-server
COPY --from=builder ${CS_PATH} ${CS_PATH}


FROM ${RUNTIME_IMAGE} AS runtime

ARG CS_VERSION
ARG CS_PATH=/opt/code-server
ARG CS_DIR=/app/code-server/${CS_VERSION}
ARG GCC_PATH=/opt/gcc
ARG BUILDX_PATH=/opt/docker-buildx
ARG COMPOSE_PATH=/opt/docker-compose
ARG CS_UID=1000
ARG CS_GID=1000

COPY --from=dist ${CS_PATH}/usr/local/lib/code-server ${CS_DIR}/code-server
COPY --from=builder /opt/nodejs/usr/local ${CS_DIR}/third/node
COPY --from=gcc ${GCC_PATH}/usr/local/lib64/libstdc++.so.6* ${GCC_PATH}/usr/local/lib64/libgcc_s.so.1* ${GCC_PATH}/usr/local/lib64/libatomic.so.1* ${CS_DIR}/third/lib/
COPY npmrc* /tmp/npmrc/
COPY conf/ /tmp/conf/
COPY --from=buildx  ${BUILDX_PATH}  /
COPY --from=compose ${COMPOSE_PATH} /

RUN set -eux                                                                                                                   \
    && printf '%s\n' /usr/local/lib64 /usr/local/lib > /etc/ld.so.conf.d/code-server-local.conf                                \
    && ldconfig                                                                                                                \
    && apt-get update                                                                                                          \
    && apt-get install -y --no-install-recommends                                                                              \
        git openssh-client locales libkrb5-3 libgssapi-krb5-2                                                                  \
        libsecret-1-0 sudo gettext-base                                                                                        \
    && apt-get clean                                                                                                           \
    && rm -rf                                                                                                                  \
        /var/lib/apt/lists/*                                                                                                   \
        /var/cache/apt/archives/*                                                                                              \
    && sed -i 's/^# *\(en_US.UTF-8\)/\1/' /etc/locale.gen                                                                      \
    && locale-gen en_US.UTF-8                                                                                                  \
    && groupadd -g ${CS_GID} coder                                                                                             \
    && useradd -u ${CS_UID} -g ${CS_GID} -G sudo -m -d ${CS_DIR}/data/home -s /bin/bash coder                                  \
    && printf '%s\n' 'coder ALL=(ALL) NOPASSWD: ALL' > /etc/sudoers.d/coder                                                    \
    && chmod 0440 /etc/sudoers.d/coder                                                                                         \
    && mkdir -p ${CS_DIR}/conf ${CS_DIR}/data/home ${CS_DIR}/data/user-data ${CS_DIR}/data/extensions ${CS_DIR}/third/node/etc \
    && if [ -f /tmp/npmrc/npmrc ]; then cp /tmp/npmrc/npmrc ${CS_DIR}/third/node/etc/npmrc; fi                                 \
    && rm -rf /tmp/npmrc                                                                                                       \
    && chmod +x ${CS_DIR}/code-server/bin/code-server                                                                          \
    && envsubst '${CS_DIR} ${CS_VERSION}' < /tmp/conf/code-server.env > ${CS_DIR}/conf/code-server.env                         \
    && envsubst '${CS_DIR} ${CS_VERSION}' < /tmp/conf/code-server.service > ${CS_DIR}/conf/code-server.service                 \
    && rm -rf /tmp/conf                                                                                                        \
    && printf '%s\n' \                                                                                                         \
        "release: code-server-${CS_VERSION}" \                                                                                 \
        "version: ${CS_VERSION}" \                                                                                             \
        "source:  /app/docker/compose/astra-linux-based/code-server" \                                                         \
        > ${CS_DIR}/VERSION.txt                                                                                                \
    && chown -R coder:coder ${CS_DIR}/data


ENV \
    CS_DIR=${CS_DIR}                                                                                                     \
    NODE_EXTRA_CA_CERTS=/etc/ssl/certs/ca-certificates.crt                                                               \
    LANG=en_US.UTF-8                                                                                                     \
    HOME=${CS_DIR}/data/home                                                                                             \
    USER=coder                                                                                                           \
    PATH=${CS_DIR}/third/node/bin:${CS_DIR}/code-server/bin:/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin \
    LD_LIBRARY_PATH=${CS_DIR}/third/lib

EXPOSE 8080

USER coder
WORKDIR ${CS_DIR}/data/home

ENTRYPOINT [ "code-server" ]
CMD [ "." ]

