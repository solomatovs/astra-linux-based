BASE_DIR      	?= $(abspath .)
ARTIFACTS     	?= $(BASE_DIR)/artifacts
BUILD_CONTEXT 	?= $(BASE_DIR)
TEST_DIR		?= $(BASE_DIR)/test

# путь, по которому дистрибутив лежит внутри -dist образа (JAVA_PATH в Dockerfile)
JAVA_PATH		?= /opt/java


.DEFAULT_GOAL := help

# список версий, которые поддерживает сборщик
# версии собираются одна за другой, строго по цепочке:
# OpenJDK требует boot JDK ровно предыдущего мажора (N-1), перепрыгнуть нельзя,
# поэтому промежуточные звенья (9,10,13..16,19,21) собираются, даже если не нужны сами по себе
VERSION_CHAIN		?= 8 9 10 11 12 13 14 15 16 17 18 19 20 21 22
# целевые версии, ради которых существует цепочка
TARGET_VERSIONS		?= 8 12 17 18 20 22
# целевая версия сборки. по умолчанию берется последняя версия
# соответственно для ее сборки потребуется собрать все предыдущие версии в цепочки
VERSION      		?= $(lastword $(VERSION_CHAIN))
# базовый url адрес для скачивания исходников собираемых в Dockerfile
BASE_URL			?= https://github.com/openjdk
# выполнять ли сброс кэша: docker build --no-cache
NO_CACHE			?= 0

# maven и gradle кладутся в образ официальными бинарными дистрибутивами:
# оба написаны на java и собрать их из исходников без них же самих нельзя.
# версии общие для всей цепочки, качаются один раз
MAVEN_VERSION		?= 3.9.9
GRADLE_VERSION		?= 8.10.2
MAVEN_URL			?= https://archive.apache.org/dist/maven/maven-3
GRADLE_URL			?= https://services.gradle.org/distributions

# apt-зависимости для сборки OpenJDK (одинаковы для всех версий)
# make из репозитория (4.1) обязателен: сборки JDK <= 15 несовместимы с GNU make 4.3+
# (ломается генерация *.vardeps), а в образе dmp/gcc лежит make 4.4.1
APT_DEPS			?= autoconf make file zip unzip cpio curl pkg-config \
					   libx11-dev libxext-dev libxrender-dev libxrandr-dev libxtst-dev libxi-dev libxt-dev \
					   libcups2-dev libfontconfig1-dev libfreetype6-dev libasound2-dev libelf-dev
# apt-зависимости рантайма: JDK динамически линкуется с fontconfig/freetype/X11/alsa
RUNTIME_APT_DEPS	?= libfreetype6 libfontconfig1 libx11-6 libxext6 libxrender1 libxtst6 libxi6 libasound2 zlib1g

GLIBC_IMAGE			?= dmp/glibc:2.28
# тулчейн для сборки. старые JDK не собираются новым gcc и наоборот
GCC_OLD_IMAGE		?= dmp/gcc:8.5.0
GCC_NEW_IMAGE		?= dmp/gcc:11.5.0

# зависимости для сборки разных версии
# версия      runtime_image     gcc_image           boot_image (jdk N-1)  repo    tag
DEPS_8	:= $(GLIBC_IMAGE) $(GCC_OLD_IMAGE) $(GLIBC_IMAGE)      jdk8u   jdk8u452-ga
DEPS_9	:= $(GLIBC_IMAGE) $(GCC_OLD_IMAGE) dmp/java:8-dist      jdk9    jdk-9+181
DEPS_10	:= $(GLIBC_IMAGE) $(GCC_OLD_IMAGE) dmp/java:9-dist      jdk10   jdk-10+46
DEPS_11	:= $(GLIBC_IMAGE) $(GCC_OLD_IMAGE) dmp/java:10-dist     jdk11u  jdk-11.0.25-ga
DEPS_12	:= $(GLIBC_IMAGE) $(GCC_OLD_IMAGE) dmp/java:11-dist     jdk12u  jdk-12.0.2-ga
DEPS_13	:= $(GLIBC_IMAGE) $(GCC_OLD_IMAGE) dmp/java:12-dist     jdk13u  jdk-13.0.14-ga
DEPS_14	:= $(GLIBC_IMAGE) $(GCC_OLD_IMAGE) dmp/java:13-dist     jdk14u  jdk-14.0.2-ga
DEPS_15	:= $(GLIBC_IMAGE) $(GCC_OLD_IMAGE) dmp/java:14-dist     jdk15u  jdk-15.0.10-ga
DEPS_16	:= $(GLIBC_IMAGE) $(GCC_OLD_IMAGE) dmp/java:15-dist     jdk16u  jdk-16.0.2-ga
DEPS_17	:= $(GLIBC_IMAGE) $(GCC_NEW_IMAGE) dmp/java:16-dist     jdk17u  jdk-17.0.13-ga
DEPS_18	:= $(GLIBC_IMAGE) $(GCC_NEW_IMAGE) dmp/java:17-dist     jdk18u  jdk-18.0.2.1-ga
DEPS_19	:= $(GLIBC_IMAGE) $(GCC_NEW_IMAGE) dmp/java:18-dist     jdk19u  jdk-19.0.2-ga
DEPS_20	:= $(GLIBC_IMAGE) $(GCC_NEW_IMAGE) dmp/java:19-dist     jdk20u  jdk-20.0.2-ga
DEPS_21	:= $(GLIBC_IMAGE) $(GCC_NEW_IMAGE) dmp/java:20-dist     jdk21u  jdk-21.0.5-ga
DEPS_22	:= $(GLIBC_IMAGE) $(GCC_NEW_IMAGE) dmp/java:21-dist     jdk22u  jdk-22.0.2-ga

# базовое название для образа
IMAGE			:= dmp/java
FIRST_VERSION	:= $(firstword $(VERSION_CHAIN))
NO_CACHE_TEXT	:= $(if $(filter 1,$(NO_CACHE)),--no-cache,)


# helper: поиск предыдущей версии в VERSION_CHAIN
prev_of = $(shell echo "$(VERSION_CHAIN)" | awk -v v='$(1)' '{for(i=1;i<=NF;i++){if($$i==v){print p;exit};p=$$i}}')
# helper: git-тег openjdk -> docker-тег (jdk8u452-ga -> 8u452, jdk-17.0.13-ga -> 17.0.13, jdk-9+181 -> 9-181)
full_of = $(shell echo '$(1)' | sed -e 's/^jdk-\?//' -e 's/-ga$$//' -e 's/+/-/g')


.PHONY: help
help:
	@echo "make:"
	@echo "  make all        собрать всё"
	@echo "  make sources    скачать исходники для цепочки вплоть до $(VERSION)"
	@echo "  make dist       собрать цепочку -dist образов одну за другой до $(VERSION)"
	@echo "  make extract    достать дистрибутив ($(JAVA_PATH)) из -dist образа в $(ARTIFACTS)/build/<версия>"
	@echo "  make build      собрать образ $(IMAGE)"
	@echo "  make test       проверить JDK: javac + java + jar + mvn/gradle в образе $(IMAGE)"
	@echo "  make targets    собрать и протестировать только целевые версии: $(TARGET_VERSIONS)"
	@echo ""
	@echo "  Цепочка: $(VERSION_CHAIN)"
	@echo "  Целевые версии: $(TARGET_VERSIONS)"
	@echo "  Первое звено ($(FIRST_VERSION)) — bootstrap через openjdk-8-jdk из apt,"
	@echo "  каждое следующее собирается предыдущим JDK (OpenJDK требует boot JDK N-1)."
	@echo "  Компилятор C/C++ и boot JDK задаются пословно в DEPS_<версия>."
	@echo "  Каждый образ тегируется мажором и полной версией (напр. 17 и 17.0.13)."
	@echo "  В образ кладутся maven $(MAVEN_VERSION) и gradle $(GRADLE_VERSION);"
	@echo "  maven-settings.xml и init.gradle запекаются, если подложены рядом с Makefile."
	@echo "  Другая версия: make all VERSION=17"
	@echo ""


.PHONY: debug
debug:
	$(info BASE_DIR       = $(BASE_DIR))
	$(info ARTIFACTS      = $(ARTIFACTS))
	$(info JAVA_PATH      = $(JAVA_PATH))
	$(info BUILD_CONTEXT  = $(BUILD_CONTEXT))
	$(info TEST_DIR       = $(TEST_DIR))
	$(info VERSION_CHAIN  = $(VERSION_CHAIN))
	$(info TARGET_VERSION = $(TARGET_VERSIONS))
	$(info VERSION        = $(VERSION))
	$(info FIRST_VERSION  = $(FIRST_VERSION))
	$(info BASE_URL       = $(BASE_URL))
	$(info NO_CACHE       = $(NO_CACHE))
	$(info NO_CACHE_TEXT  = $(NO_CACHE_TEXT))
	$(info MAVEN_VERSION  = $(MAVEN_VERSION))
	$(info GRADLE_VERSION = $(GRADLE_VERSION))
	$(info APT_DEPS       = $(APT_DEPS))
	$(info RUNTIME_DEPS   = $(RUNTIME_APT_DEPS))
	$(info IMAGE          = $(IMAGE):$(VERSION))
	$(info DEPS($(VERSION)) = $(DEPS_$(VERSION)))
	$(info FULL($(VERSION)) = $(call full_of,$(word 5,$(DEPS_$(VERSION)))))
	@true


# Скачиваем зависимости для сборки дистрибутива
.PHONY: sources
sources: sources-$(VERSION)

.ONESHELL:
sources-%:
	@current=$(*)
	@prev='$(call prev_of,$*)'
	# вызываем рекурсивно предыдущую версию
	[ -n "$$prev" ] && $(MAKE) sources-$$prev || true
	set -e
	set -- $(DEPS_$*)
	repo=$$4; tag=$$5
	mkdir -p $(ARTIFACTS)/src
	[ -f "$(ARTIFACTS)/src/jdk-$$current.tar.gz" ] || curl -fL -o "$(ARTIFACTS)/src/jdk-$$current.tar.gz" "$(BASE_URL)/$$repo/archive/refs/tags/$$tag.tar.gz"
	# maven/gradle общие для всей цепочки, качаются один раз
	[ -f "$(ARTIFACTS)/src/apache-maven-$(MAVEN_VERSION)-bin.tar.gz" ] || curl -fL -o "$(ARTIFACTS)/src/apache-maven-$(MAVEN_VERSION)-bin.tar.gz" "$(MAVEN_URL)/$(MAVEN_VERSION)/binaries/apache-maven-$(MAVEN_VERSION)-bin.tar.gz"
	[ -f "$(ARTIFACTS)/src/gradle-$(GRADLE_VERSION)-bin.zip" ] || curl -fL -o "$(ARTIFACTS)/src/gradle-$(GRADLE_VERSION)-bin.zip" "$(GRADLE_URL)/gradle-$(GRADLE_VERSION)-bin.zip"


# docker build --target dist - дистрибутив с скомпилированными исходниками (без установки в систему)
.PHONY: dist
dist: dist-$(VERSION)

.ONESHELL:
dist-%: sources-%
	@current=$(*)
	prev='$(call prev_of,$*)'
	# рекурсивно собираем предыдущую версию
	[ -n "$$prev" ] && $(MAKE) dist-$$prev || true
	set -e
	set -- $(DEPS_$*)
	full='$(call full_of,$(word 5,$(DEPS_$*)))'
	docker build -f Dockerfile $(NO_CACHE_TEXT) --target dist		\
		--build-arg RUNTIME_IMAGE=$$1								\
		--build-arg BUILD_IMAGE=$$2									\
		--build-arg BOOT_IMAGE=$$3									\
		--build-arg JAVA_VERSION=$$current							\
		--build-arg JAVA_FULL_VERSION=$$full						\
		--build-arg MAVEN_VERSION=$(MAVEN_VERSION)					\
		--build-arg GRADLE_VERSION=$(GRADLE_VERSION)				\
		--build-arg APT_DEPS="$(APT_DEPS)"							\
		--build-arg RUNTIME_APT_DEPS="$(RUNTIME_APT_DEPS)"			\
		--tag $(IMAGE):$$current-dist								\
		--tag $(IMAGE):$$full-dist									\
		--progress plain $(BUILD_CONTEXT)


# извлечь скомпилированный дистрибутив (/opt/java) из -dist образа в artifacts/build/<версия>
.PHONY: extract
extract: extract-$(VERSION)

.ONESHELL:
extract-%: dist-%
	@current=$(*)
	set -e
	img='$(IMAGE):'"$$current"'-dist'
	dst='$(ARTIFACTS)/build/'"$$current"
	# создаём временный контейнер из -dist образа (без запуска) и копируем из него дерево
	cid=$$(docker create $$img)
	trap 'docker rm -f $$cid >/dev/null 2>&1 || true' EXIT
	rm -rf "$$dst"
	mkdir -p "$$dst"
	docker cp "$$cid:$(JAVA_PATH)/." "$$dst/"
	echo "extracted $$img:$(JAVA_PATH) -> $$dst"
	du -h --max-depth=1 "$$dst" || true


# docker build - истоговый образ с установленным ПО (установка из дистрибутива)
.PHONY: build
build: build-$(VERSION)

.ONESHELL:
build-%: dist-%
	@current=$(*)
	prev='$(call prev_of,$*)'
	# рекурсивно собираем предыдущую версию
	[ -n "$$prev" ] && $(MAKE) build-$$prev || true
	set -e
	set -- $(DEPS_$*)
	full='$(call full_of,$(word 5,$(DEPS_$*)))'
	docker build -f Dockerfile $(NO_CACHE_TEXT)						\
		--build-arg RUNTIME_IMAGE=$$1								\
		--build-arg BUILD_IMAGE=$$2									\
		--build-arg BOOT_IMAGE=$$3									\
		--build-arg JAVA_VERSION=$$current							\
		--build-arg JAVA_FULL_VERSION=$$full						\
		--build-arg MAVEN_VERSION=$(MAVEN_VERSION)					\
		--build-arg GRADLE_VERSION=$(GRADLE_VERSION)				\
		--build-arg APT_DEPS="$(APT_DEPS)"							\
		--build-arg RUNTIME_APT_DEPS="$(RUNTIME_APT_DEPS)"			\
		--tag $(IMAGE):$$current									\
		--tag $(IMAGE):$$full										\
		--progress plain $(BUILD_CONTEXT)


# тестирование собранного образа
.PHONY: test
test: test-$(VERSION)

.ONESHELL:
test-%: build-%
	@current=$(*)
	docker run --rm --entrypoint /bin/bash -v $(TEST_DIR):/test:ro $(IMAGE):$$current -euc '\
		echo "== versions =="; 					\
		java -version 2>&1 | head -3; 			\
		javac -version 2>&1; 					\
		mvn --version 2>&1 | head -1; 			\
		gradle --version 2>&1 | grep -E "^Gradle "; \
		echo "JAVA_HOME=$$JAVA_HOME"; 			\
		echo "MAVEN_HOME=$$MAVEN_HOME"; 		\
		echo "GRADLE_HOME=$$GRADLE_HOME"; 		\
		echo "== compile =="; 					\
		mkdir -p /tmp/classes; 					\
		javac -d /tmp/classes /test/Hello.java;	\
		echo "== jar =="; 						\
		jar cfe /tmp/hello.jar Hello -C /tmp/classes .; \
		echo "== run =="; 						\
		java -jar /tmp/hello.jar'


# собрать и протестировать только целевые версии (цепочка подтянется рекурсивно)
.PHONY: targets
targets:
	@for v in $(TARGET_VERSIONS); do $(MAKE) test VERSION=$$v || exit 1; done


.PHONY: clean
clean: clean-$(VERSION)

.ONESHELL:
clean-%:
	@current=$(*)
	@prev='$(call prev_of,$*)'
	# вызываем рекурсивно предыдущую версию
	[ -n "$$prev" ] && $(MAKE) clean-$$prev || true
	set -e
	rm -f "$(ARTIFACTS)/src/jdk-$$current.tar.gz"


.PHONY: all
all: sources dist build test
