# code-server
ARG RUNTIME_IMAGE=dmp/nodejs:22.22.1
ARG GCC_IMAGE=dmp/gcc:12.4.0-dist
ARG PYTHON_IMAGE=dmp/python:3.10-dist

FROM ${GCC_IMAGE}       AS gcc
FROM ${PYTHON_IMAGE}    AS python
FROM ${RUNTIME_IMAGE}   AS base
FROM base AS builder

ARG CS_VERSION
ARG CS_PATH=/opt/code-server
ARG VSCODE_SHA
ARG RIPGREP_ARCHIVE
ARG ELECTRON_TARGET
ARG NODE_TARGET
ARG SRC=/usr/src/code-server
ARG GCC_PATH=/opt/gcc
ARG PYTHON_PATH=/opt/python

# тулчейн для node-gyp: gcc/g++/make/binutils + python3 (интерпретатор gyp)
COPY --from=gcc ${GCC_PATH}/usr/local/ /usr/local/
COPY --from=gcc /usr/include/ /usr/include/
COPY --from=python ${PYTHON_PATH}/usr/local/ /usr/local/
RUN ln -sf gcc /usr/local/bin/cc            \
    && ln -sf g++ /usr/local/bin/c++        \
    && printf '%s\n'                        \
        /usr/local/lib64                    \
        /usr/local/lib                      \
        > /etc/ld.so.conf.d/gcc-local.conf  \
    && ldconfig


# git — сборочные скрипты зовут rev-parse/checkout/config, quilt накатывает patches/,
# jq и rsync нужны build-vscode.sh и build-release.sh,
# остальное — заголовки для нативных модулей vscode
RUN apt-get update                                              \
    && apt-get install -y --no-install-recommends               \
        git quilt jq rsync unzip pkg-config                     \
        libkrb5-dev libx11-dev libxkbfile-dev libsecret-1-dev   \
    && apt-get clean                                            \
    && rm -rf /var/lib/apt/lists/* /var/cache/apt/archives/*

# npmrc
COPY artifacts/npmrc* /tmp/npmrc/
RUN if [ -f /tmp/npmrc/npmrc ]; then cp /tmp/npmrc/npmrc /usr/local/etc/npmrc; fi \
    && rm -rf /tmp/npmrc

# install.js пакета electron качает с github бинарник десктопного приложения
# (~100 МБ). собирается vscode-reh-web — серверная часть, сам electron ей не
# нужен, нужны только его заголовки для node-gyp, а они уже в кэше
ENV ELECTRON_SKIP_BINARY_DOWNLOAD=1

COPY artifacts/src/code-server-${CS_VERSION}-src.tar.gz /tmp/src/code-server.tar.gz
COPY artifacts/src/vscode-${VSCODE_SHA}.tar.gz /tmp/src/vscode.tar.gz
COPY artifacts/src/${RIPGREP_ARCHIVE} /tmp/src/ripgrep.tar.gz
COPY artifacts/src/electron-v${ELECTRON_TARGET}-headers.tar.gz /tmp/src/electron-headers.tar.gz
COPY artifacts/src/builtin-${CS_VERSION}/ /tmp/src/vsix/
COPY artifacts/src/gitdeps-${CS_VERSION}/ /tmp/src/gitdeps/
COPY gitdeps-rewrite.py /usr/local/share/gitdeps-rewrite.py

# lib/vscode в апстриме — сабмодуль, тарбол распаковывается на его место.
# сборочные скрипты работают с git: build-vscode.sh берет rev-parse HEAD для
# product.json и делает checkout product.json, postinstall.ts зовет git config.
# сюда же — зеркала зависимостей, которые ставятся с github мимо реестра;
# gitdeps-rewrite.py переписывает спеки на них, подробности в самом скрипте
RUN set -eux;                                                                       \
    mkdir -p ${SRC};                                                                \
    tar -xzf /tmp/src/code-server.tar.gz -C ${SRC} --strip-components=1;             \
    tar -xzf /tmp/src/vscode.tar.gz -C ${SRC}/lib/vscode --strip-components=1;       \
    git config --global user.email builder@localhost;                               \
    git config --global user.name builder;                                          \
    git config --global init.defaultBranch main;                                    \
    mkdir -p /usr/src/gitdeps;                                                      \
    for t in /tmp/src/gitdeps/*.tar.gz; do tar -xzf "$t" -C /usr/src/gitdeps; done; \
    python3 /usr/local/share/gitdeps-rewrite.py ${SRC} /usr/src/gitdeps;             \
    cd ${SRC}/lib/vscode && git init -q && git add -A && git commit -qm ${VSCODE_SHA}; \
    cd ${SRC} && git init -q && git add -A && git commit -qm ${CS_VERSION}

# патчи code-server поверх vscode, -p1 от корня репозитория
RUN set -eux;                                                                       \
    cd ${SRC};                                                                      \
    QUILT_PATCHES=patches quilt push -a;                                            \
    cd lib/vscode && git add -A && git commit -qm patched

# всё, что апстрим качает мимо npm-реестра, подкладывается заранее:
#   node-gyp    — заголовки в кэше, иначе пойдет на nodejs.org и electronjs.org.
#                 installVersion читается из самого node-gyp, иначе он сочтет
#                 кэш устаревшим и полезет качать
#   .build/node — gulp кладет этот бинарник внутрь reh-бандла, а если каталога
#                 нет, качает node с nodejs.org. версия совпадает с нашей
#   builtInExtensions — getExtensionStream берет каталог с диска, если версия
#                 в package.json совпадает с product.json, иначе идет в marketplace
RUN set -eux;                                                                       \
    INSTALL_VERSION="$(node -p 'require("/usr/local/lib/node_modules/npm/node_modules/node-gyp/package.json").installVersion')"; \
    D=/root/.cache/node-gyp/${NODE_TARGET};                                         \
    mkdir -p "$D/include";                                                          \
    cp -a /usr/local/include/node "$D/include/";                                    \
    echo "$INSTALL_VERSION" > "$D/installVersion";                                  \
    D=/root/.cache/node-gyp/${ELECTRON_TARGET};                                     \
    mkdir -p "$D";                                                                  \
    tar -xzf /tmp/src/electron-headers.tar.gz -C "$D" --strip-components=1;         \
    echo "$INSTALL_VERSION" > "$D/installVersion";                                  \
    D=${SRC}/lib/vscode/.build/node/v${NODE_TARGET}/linux-x64;                      \
    mkdir -p "$D";                                                                  \
    cp /usr/local/bin/node "$D/node";                                               \
    for vsix in /tmp/src/vsix/*.vsix; do                                            \
        name="$(basename "$vsix" .vsix)"; name="${name%-*}";                        \
        D=${SRC}/lib/vscode/.build/builtInExtensions/$name;                         \
        mkdir -p "$D";                                                              \
        unzip -q "$vsix" 'extension/*' -d /tmp/vsix-unpack;                         \
        mv /tmp/vsix-unpack/extension/* "$D/";                                      \
        rm -rf /tmp/vsix-unpack;                                                    \
    done

# ставим зависимости без install-скриптов, подкладываем ripgrep во все его копии
# (postinstall @vscode/ripgrep уходит на api.github.com, но при наличии bin/
# выходит сразу), и только потом собираем нативные модули
RUN set -eux;                                                                       \
    mkdir -p /tmp/rg && tar -xzf /tmp/src/ripgrep.tar.gz -C /tmp/rg rg;             \
    export npm_config_ignore_scripts=true;                                          \
    cd ${SRC} && npm install;                                                       \
    cd ${SRC}/lib/vscode && npm install;                                            \
    node build/npm/postinstall.ts;                                                  \
    find ${SRC} -type d -path '*/node_modules/@vscode/ripgrep'                      \
        -exec sh -c 'mkdir -p "$1/bin" && cp /tmp/rg/rg "$1/bin/rg"' _ {} \; ;      \
    ${SRC}/lib/vscode/node_modules/@vscode/ripgrep/bin/rg --version

# нативные модули: корень vscode собирается под electron, remote — под node,
# target и disturl берутся из .npmrc каждого каталога, заголовки уже в кэше
RUN set -eux;                                                                       \
    for d in ${SRC} ${SRC}/lib/vscode ${SRC}/lib/vscode/build                       \
             ${SRC}/lib/vscode/remote ${SRC}/lib/vscode/extensions; do              \
        [ -d "$d/node_modules" ] || continue;                                       \
        cd "$d" && npm rebuild --unsafe-perm;                                       \
    done

# сборка: tsc для самого code-server, затем gulp-таргеты vscode и упаковка
# в release/ (KEEP_MODULES=1 — с node_modules и запускалкой bin/code-server)
RUN set -eux;                                                                       \
    cd ${SRC};                                                                      \
    export VERSION=${CS_VERSION};                                                   \
    npm run build;                                                                  \
    npm run build:vscode;                                                           \
    KEEP_MODULES=1 npm run release

RUN set -eux;                                                                       \
    mkdir -p ${CS_PATH}/usr/local/lib ${CS_PATH}/usr/local/bin;                     \
    cp -a ${SRC}/release ${CS_PATH}/usr/local/lib/code-server;                      \
    ln -sf ../lib/code-server/bin/code-server ${CS_PATH}/usr/local/bin/code-server; \
    find ${CS_PATH} -type d \( -name obj -o -name obj.target -o -name .deps \)      \
        -prune -exec rm -rf {} +;                                                   \
    find ${CS_PATH} -type f \( -name '*.o' -o -name '*.a' \) -delete;               \
    du -h --max-depth=1 ${CS_PATH}/usr/local/lib/code-server


# dist: только собранный /opt/code-server без исходников и сборочного тулчейна
FROM base AS dist
ARG CS_PATH=/opt/code-server
COPY --from=builder ${CS_PATH} ${CS_PATH}


FROM base AS runtime

ARG CS_PATH=/opt/code-server
ARG GCC_PATH=/opt/gcc
ARG CS_UID=1000
ARG CS_GID=1000

COPY --from=dist ${CS_PATH} /

# нативные модули собраны gcc 12 — libstdc++/libgcc берём из того же тулчейна
COPY --from=gcc ${GCC_PATH}/usr/local/lib64/libstdc++.so.6* ${GCC_PATH}/usr/local/lib64/libgcc_s.so.1* /usr/local/lib64/
RUN printf '%s\n'                                   \
        /usr/local/lib64                            \
        /usr/local/lib                              \
        > /etc/ld.so.conf.d/code-server-local.conf  \
    && ldconfig

# git/openssh — source control и ssh-remote в VS Code; libkrb5-3 — рантайм для kerberos
RUN apt-get update                                              \
    && apt-get install -y --no-install-recommends               \
        git openssh-client locales                              \
        libkrb5-3 libgssapi-krb5-2 libsecret-1-0                \
        sudo                                                    \
    && apt-get clean                                            \
    && rm -rf /var/lib/apt/lists/* /var/cache/apt/archives/*    \
    && sed -i 's/^# *\(en_US.UTF-8\)/\1/' /etc/locale.gen       \
    && locale-gen en_US.UTF-8

ENV LANG=en_US.UTF-8

# Непривилегированный пользователь
RUN groupadd -g ${CS_GID} coder \
    && useradd -u ${CS_UID} -g ${CS_GID} -G sudo -m -d /home/coder -s /bin/bash coder \
    && mkdir -p /home/coder/.config/code-server /home/coder/.local/share/code-server \
    && chown -R coder:coder /home/coder \
    && printf '%s\n' 'coder ALL=(ALL) NOPASSWD: ALL' > /etc/sudoers.d/coder \
    && chmod 0440 /etc/sudoers.d/coder

# от апстримного ci/release-image/entrypoint.sh остаётся только hook ENTRYPOINTD:
# fixuid и dumb-init качаются с github и в закрытом контуре недоступны
RUN printf '%s\n' \
    '#!/bin/sh' \
    'set -eu' \
    'if [ -n "${ENTRYPOINTD:-}" ] && [ -d "${ENTRYPOINTD}" ]; then' \
    '  find "${ENTRYPOINTD}" -type f -executable -print -exec {} \;' \
    'fi' \
    'exec /usr/local/bin/code-server "$@"' \
    > /usr/local/bin/code-server-entrypoint.sh \
    && chmod +x /usr/local/bin/code-server-entrypoint.sh

ENV HOME=/home/coder
ENV USER=coder
# скрипты подготовки workspace, запускаются перед стартом сервера
ENV ENTRYPOINTD=/home/coder/entrypoint.d
EXPOSE 8080

USER coder
WORKDIR /home/coder

ENTRYPOINT [ "/usr/local/bin/code-server-entrypoint.sh", "--bind-addr", "0.0.0.0:8080" ]
CMD [ "." ]
