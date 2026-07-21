# claude-code-router
ARG RUNTIME_IMAGE=dmp/nodejs:22
ARG CCR_PATH=/opt/claude-code-router

FROM ${RUNTIME_IMAGE} AS base
FROM base AS dist

ARG CCR_VERSION
ARG CCR_PATH
ARG MONACO_VERSION

COPY artifacts/src/claude-code-router-${CCR_VERSION}.tgz /tmp/ccr.tgz

# Ассеты, которые апстрим-фронт тянет из интернета в рантайме (monaco с jsdelivr,
# providers.json с r2.dev, combo.css с nerdfonts). Кладём их рядом с index.html:
# CCR отдаёт всю папку dist через @fastify/static с prefix=/ui/, поэтому файлы
# автоматически доступны как /ui/vs, /ui/providers.json, /ui/combo.css,
# а optimize-ui.js переписывает ссылки в бандле на эти локальные пути.
COPY artifacts/src/monaco-editor-${MONACO_VERSION}.tgz /tmp/monaco.tgz
COPY artifacts/src/providers.json artifacts/src/combo.css /tmp/assets/

# npmrc
COPY artifacts/npmrc* /tmp/npmrc/
# вне /tmp: шаг ниже подчищает /tmp за собой
COPY optimize-ui.js /usr/local/share/optimize-ui.js
RUN set -eux;                                                                       \
    if [ -f /tmp/npmrc/npmrc ]; then cp /tmp/npmrc/npmrc /usr/local/etc/npmrc; fi;  \
    npm install -g /tmp/ccr.tgz --prefix ${CCR_PATH}/usr/local;                     \
    DIST=${CCR_PATH}/usr/local/lib/node_modules/@musistudio/claude-code-router/dist; \
    # из тарбола monaco нужен только min/vs — собранный AMD-бандл, который грузит loader
    mkdir -p /tmp/monaco "$DIST/vs";                                                \
    tar -xzf /tmp/monaco.tgz -C /tmp/monaco package/min/vs;                         \
    cp -a /tmp/monaco/package/min/vs/. "$DIST/vs/";                                 \
    cp /tmp/assets/providers.json /tmp/assets/combo.css "$DIST/";                   \
    node /usr/local/share/optimize-ui.js "$DIST/index.html";                        \
    rm -rf /tmp/*


FROM base AS runtime

ARG CCR_PATH
ARG CCR_UID=1000
ARG CCR_GID=1000

COPY --from=dist ${CCR_PATH} /

# Непривилегированный пользователь
RUN groupadd -g ${CCR_GID} ccr \
    && useradd -u ${CCR_UID} -g ${CCR_GID} -m -d /home/ccr -s /usr/sbin/nologin ccr \
    && mkdir -p /home/ccr/.claude-code-router \
    && chown -R ccr:ccr /home/ccr

RUN printf '%s\n' \
    '#!/bin/sh' \
    'if [ "$1" = "start" ]; then rm -f "${HOME:-/home/ccr}/.claude-code-router/.claude-code-router.pid"; fi' \
    'exec /usr/local/bin/ccr "$@"' \
    > /usr/local/bin/ccr-entrypoint.sh \
    && chmod +x /usr/local/bin/ccr-entrypoint.sh

ENV HOME=/home/ccr
# config монтируется в ~/.claude-code-router
ENV HOST=0.0.0.0
EXPOSE 3456

USER ccr
WORKDIR /home/ccr

ENTRYPOINT [ "/usr/local/bin/ccr-entrypoint.sh" ]
CMD [ "start" ]
