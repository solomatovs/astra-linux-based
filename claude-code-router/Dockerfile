# claude-code-router
ARG RUNTIME_IMAGE=dmp/nodejs:22
ARG CCR_PATH=/opt/claude-code-router

FROM ${RUNTIME_IMAGE} AS base
FROM base AS dist

ARG CCR_VERSION
ARG CCR_PATH

COPY artifacts/src/claude-code-router-${CCR_VERSION}.tgz /tmp/ccr.tgz

# npmrc
COPY artifacts/npmrc* /tmp/npmrc/
RUN if [ -f /tmp/npmrc/npmrc ]; then cp /tmp/npmrc/npmrc /usr/local/etc/npmrc; fi \
    && npm install -g /tmp/ccr.tgz --prefix ${CCR_PATH}/usr/local \
    && rm -rf /tmp/*


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
