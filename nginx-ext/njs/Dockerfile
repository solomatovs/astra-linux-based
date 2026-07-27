# njs: JavaScript-скриптинг (ngx_http_js_module / ngx_stream_js_module), поверх образа nginx
ARG NGINX_VERSION
ARG BASE_IMAGE=dmp/nginx:${NGINX_VERSION}

# build: собираем модули в дереве исходников из nginx:*-dist
FROM dmp/nginx:${NGINX_VERSION}-dist AS build

ARG NJS_VERSION

COPY artifacts/src/njs-${NJS_VERSION}.tar.gz /tmp/njs.tar.gz
RUN mkdir -p /usr/src/njs \
    && tar -xf /tmp/njs.tar.gz -C /usr/src/njs --strip-components=1 \
    && rm -f /tmp/njs.tar.gz

COPY build.sh /tmp/build.sh
RUN chmod +x /tmp/build.sh && /tmp/build.sh


# runtime: базовый образ + модули
FROM ${BASE_IMAGE} AS runtime

COPY --from=build /opt/ext/ /
RUN ldconfig
