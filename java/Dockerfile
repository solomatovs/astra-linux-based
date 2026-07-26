# java (OpenJDK)
ARG RUNTIME_IMAGE=dmp/glibc:2.28
ARG BUILD_IMAGE=dmp/gcc:8.5.0
ARG BOOT_IMAGE=dmp/glibc:2.28

FROM ${BOOT_IMAGE} AS prev
ARG JAVA_PATH=/opt/java

RUN mkdir -p /seed \
    && if [ -d "${JAVA_PATH}" ] && [ -n "$(ls -A "${JAVA_PATH}" 2>/dev/null)" ]; then \
        cp -a "${JAVA_PATH}/." /seed/; \
    fi

FROM ${RUNTIME_IMAGE} AS base
FROM ${BUILD_IMAGE} AS builder

ARG JAVA_VERSION
ARG JAVA_FULL_VERSION
ARG MAVEN_VERSION
ARG GRADLE_VERSION
ARG APT_DEPS
ARG JAVA_PATH=/opt/java


# копируем jdk из предыдущего образа, если он там был
# и в зависимости от того, есть ли boot jdk, ставим разный набор apt зависимостей:
# для первого звена цепочки boot jdk брать неоткуда — берём openjdk-8-jdk из репозитория
COPY --from=prev /seed/ /
RUN apt-get update                                                  \
    && if [ -x /usr/local/java/bin/javac ]; then                    \
        apt-get install -y --no-install-recommends ${APT_DEPS};     \
    else                                                            \
        apt-get install -y --no-install-recommends                  \
            ${APT_DEPS} openjdk-8-jdk ant;                          \
    fi                                                              \
    && apt-get clean                                                \
    && rm -rf /var/lib/apt/lists/* /var/cache/apt/archives/*

# фиксируем путь до boot jdk один раз, чтобы дальше не дублировать условие
RUN if [ -x /usr/local/java/bin/javac ]; then                       \
        echo /usr/local/java > /boot-jdk;                           \
    else                                                            \
        echo /usr/lib/jvm/java-8-openjdk-amd64 > /boot-jdk;         \
    fi                                                              \
    && "$(cat /boot-jdk)"/bin/java -version

# OpenJDK
#
# общие для всех версий опции configure: release-сборка только server-варианта JVM,
# статический libstdc++ (в рантайм-образе gcc нет) и без debug-символов.
# у jdk8 набор ключей другой: debug-символы и freetype задаются иначе,
# плюс hotspot требует отключить оптимизации, ломающие его на gcc >= 6.
# результат сборки jdk8 лежит в images/j2sdk-image, у jdk9+ — в images/jdk.
# собираем системным make 4.1 (/usr/bin/make), а не make 4.4.1 из образа dmp/gcc:
# сборки JDK <= 15 несовместимы с GNU make 4.3+ (ломается генерация *.vardeps).
# GenerateCurrencyData отказывается собирать таблицу валют, если дата перехода
# отстоит от "сегодня" больше чем на 10 лет — старые ветки JDK из-за этого
# перестают собираться со временем, поэтому ослабляем проверку до 1000 лет.
COPY artifacts/src/jdk-${JAVA_VERSION}.tar.gz /tmp/src/jdk.tar.gz
RUN set -eux;                                                       \
    mkdir -p /tmp/jdk-src;                                          \
    tar -xf /tmp/src/jdk.tar.gz -C /tmp/jdk-src --strip-components=1; \
    cd /tmp/jdk-src;                                                \
    find . -path '*build/tools/generatecurrencydata/GenerateCurrencyData.java' \
        -exec sed -i 's/((long) 10) \* 365/((long) 1000) * 365/' {} +; \
    BOOT_JDK="$(cat /boot-jdk)";                                    \
    MAKE_BIN=/usr/bin/make;                                         \
    MAKE_EXTRA="";                                                  \
    set -- --with-boot-jdk="$BOOT_JDK"                              \
           --with-jvm-variants=server                               \
           --with-debug-level=release                               \
           --with-stdc++lib=static;                                 \
    if [ "${JAVA_VERSION}" = "8" ]; then                            \
        set -- "$@" --disable-debug-symbols                         \
               --disable-zip-debug-info                             \
               --with-target-bits=64                                \
               --with-freetype-include=/usr/include/freetype2       \
               --with-freetype-lib=/usr/lib/x86_64-linux-gnu        \
               --with-extra-cflags="-w -fno-lifetime-dse -fno-delete-null-pointer-checks" \
               --with-extra-cxxflags="-w -fno-lifetime-dse -fno-delete-null-pointer-checks"; \
        MAKE_EXTRA="WARNINGS_ARE_ERRORS= DISABLE_HOTSPOT_OS_VERSION_CHECK=ok"; \
    else                                                            \
        set -- "$@" --with-native-debug-symbols=none                \
               --disable-warnings-as-errors;                        \
        if [ "${JAVA_VERSION}" -le 15 ]; then                       \
            set -- "$@" --with-extra-cflags=-fcommon                \
                   --with-extra-cxxflags=-fcommon;                  \
        fi;                                                         \
    fi;                                                             \
    MAKE="$MAKE_BIN" bash ./configure "$@";                         \
    "$MAKE_BIN" JOBS="$(nproc)" ${MAKE_EXTRA} images;               \
    IMG="$(ls -d build/*/images/jdk 2>/dev/null || ls -d build/*/images/j2sdk-image)"; \
    mkdir -p "${JAVA_PATH}/usr/local/java";                         \
    cp -a "$IMG"/. "${JAVA_PATH}/usr/local/java/";                  \
    echo "${JAVA_FULL_VERSION}" > "${JAVA_PATH}/usr/local/java/.dmp-version"; \
    "${JAVA_PATH}/usr/local/java/bin/java" -version;                \
    du -h --max-depth=1 "${JAVA_PATH}";                             \
    cd /;                                                           \
    rm -rf /tmp/*

# maven и gradle — официальные бинарные дистрибутивы: оба написаны на java,
# собрать их из исходников без них же самих нельзя. Кладём рядом с JDK в тот же
# /opt/java, чтобы они уехали в -dist и в runtime одним COPY. Шаг идёт ПОСЛЕ сборки
# JDK намеренно: смена версии maven/gradle не должна инвалидировать кэш сборки JDK
COPY artifacts/src/apache-maven-${MAVEN_VERSION}-bin.tar.gz /tmp/src/maven.tar.gz
COPY artifacts/src/gradle-${GRADLE_VERSION}-bin.zip /tmp/src/gradle.zip
RUN set -eux;                                                       \
    mkdir -p "${JAVA_PATH}/usr/local/maven";                        \
    tar -xf /tmp/src/maven.tar.gz -C "${JAVA_PATH}/usr/local/maven" --strip-components=1; \
    unzip -q /tmp/src/gradle.zip -d /tmp/gradle;                    \
    mv /tmp/gradle/gradle-${GRADLE_VERSION} "${JAVA_PATH}/usr/local/gradle"; \
    du -h --max-depth=1 "${JAVA_PATH}/usr/local";                   \
    rm -rf /tmp/*


# dist: только собранный /opt/java без сборочного тулчейна — компактный «дистрибутив»
FROM base AS dist
ARG JAVA_PATH=/opt/java
COPY --from=builder ${JAVA_PATH} ${JAVA_PATH}


FROM base AS runtime

ARG JAVA_PATH=/opt/java
ARG RUNTIME_APT_DEPS

# устанавливаем собранный jdk в runtime-образ: /opt/java/usr/local/java -> /usr/local/java
COPY --from=dist ${JAVA_PATH} /

# рантайм-библиотеки, с которыми jdk слинкован динамически (шрифты, X11, звук)
RUN apt-get update                                                  \
    && apt-get install -y --no-install-recommends ${RUNTIME_APT_DEPS} \
    && apt-get clean                                                \
    && rm -rf /var/lib/apt/lists/* /var/cache/apt/archives/*        \
    && for b in /usr/local/java/bin/* /usr/local/maven/bin/mvn      \
                /usr/local/maven/bin/mvnDebug                       \
                /usr/local/gradle/bin/gradle; do                    \
        ln -sf "$b" /usr/local/bin/"$(basename "$b")";              \
    done                                                            \
    && ldconfig

# maven-settings.xml replace
COPY maven-settings.xml* /tmp/mvnconf/
RUN if [ -f /tmp/mvnconf/maven-settings.xml ]; then                 \
        cp /tmp/mvnconf/maven-settings.xml /usr/local/maven/conf/settings.xml; \
    fi                                                              \
    && rm -rf /tmp/mvnconf

# init.gradle replace: gradle применяет любой *.gradle из ${GRADLE_HOME}/init.d
# ко всем сборкам, поэтому репозитории переопределяются без правки самих проектов
COPY init.gradle* /tmp/gradleconf/
RUN if [ -f /tmp/gradleconf/init.gradle ]; then                     \
        mkdir -p /usr/local/gradle/init.d                           \
        && cp /tmp/gradleconf/init.gradle /usr/local/gradle/init.d/init.gradle; \
    fi                                                              \
    && rm -rf /tmp/gradleconf

# java не доверяет системному trust store, у неё свой cacerts, поэтому
# корпоративный CA импортируется отдельно — иначе maven/gradle не смогут
# ходить в nexus по https, хотя curl и apt уже работают
COPY ca-chain.crt* /tmp/cacrt/
RUN if [ -f /tmp/cacrt/ca-chain.crt ] && grep -qE '^-----BEGIN CERTIFICATE-----' /tmp/cacrt/ca-chain.crt; then \
        cp /tmp/cacrt/ca-chain.crt /usr/local/share/ca-certificates/ca-chain.crt \
        && update-ca-certificates                                   \
        && mkdir -p /tmp/casplit                                    \
        && csplit -z -f /tmp/casplit/ca- -b '%02d.crt' /tmp/cacrt/ca-chain.crt '/-----BEGIN CERTIFICATE-----/' '{*}' \
        && for c in /tmp/casplit/ca-*.crt; do                       \
            keytool -importcert -noprompt -trustcacerts             \
                -cacerts -storepass changeit                        \
                -alias "dmp-$(basename "$c" .crt)" -file "$c" >/dev/null 2>&1 \
                || keytool -importcert -noprompt -trustcacerts      \
                    -keystore /usr/local/java/jre/lib/security/cacerts \
                    -storepass changeit                             \
                    -alias "dmp-$(basename "$c" .crt)" -file "$c";  \
        done;                                                       \
    fi                                                              \
    && rm -rf /tmp/cacrt /tmp/casplit

ENV JAVA_HOME=/usr/local/java \
    MAVEN_HOME=/usr/local/maven \
    GRADLE_HOME=/usr/local/gradle \
    PATH=/usr/local/java/bin:/usr/local/maven/bin:/usr/local/gradle/bin:${PATH}

ENTRYPOINT [ "/usr/local/java/bin/java" ]
