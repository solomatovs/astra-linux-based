BASE_DIR      	?= $(abspath .)
ARTIFACTS     	?= $(BASE_DIR)/artifacts
BUILD_CONTEXT 	?= $(BASE_DIR)
TEST_DIR		?= $(BASE_DIR)/test


.DEFAULT_GOAL := help

# список версий, которые поддерживает сборщик
# версии php независимы, но собираются одна за другой, строго по цепочке
VERSION_CHAIN		?= 7.4.33 8.0.30 8.1.31 8.2.27 8.3.16 8.4.3
# целевая версия сборки. по умолчанию берется последняя версия
# соответственно для ее сборки потребуется собрать все предыдущие версии в цепочки
VERSION      		?= $(lastword $(VERSION_CHAIN))
# базовые url адреса (конкретные версии подставляются в цели sources)
# php url
BASE_URL			?= https://www.php.net/distributions
# zlib
ZLIB_URL			?= https://github.com/madler/zlib/releases/download
# curl
CURL_URL			?= https://curl.se/download
# libpq/postgresql
LIBPQ_URL			?= https://ftp.postgresql.org/pub/source
# libzip
ZIP_URL				?= https://libzip.org/download
# composer url
COMPOSER_URL		?= https://getcomposer.org/download
# docker build --no-cache
NO_CACHE			?= 0
# apt-deps (dev)
APT_DEPS			?= pkg-config bison re2c curl file cmake bzip2	\
					libxml2-dev libssl-dev 						\
					libonig-dev libsqlite3-dev 					\
					zlib1g-dev libbz2-dev libreadline-dev		\
					libpng-dev libjpeg-dev libfreetype6-dev 	\
					libicu-dev libsodium-dev
# apt-deps (runtime)
RUNTIME_APT_DEPS	?= libxml2 libssl1.1 						\
					libonig4 libsqlite3-0 zlib1g libbz2-1.0 	\
					libreadline7 libncurses5 libpng16-16 		\
					libjpeg62-turbo libfreetype6 libicu63 		\
					libsodium23 ca-certificates unzip git

GLIBC_IMAGE			?= dmp/glibc:2.28
GCC_IMAGE			?= dmp/gcc:8.5.0-dist

# зависимости для сборки разных версии
# версия           runtime_image	build_image		zlib	curl		libpq	zip		composer
DEPS_7.4.33		:= $(GLIBC_IMAGE)	$(GCC_IMAGE)	1.3.1	8.11.1		16.6	1.11.3	2.8.4
DEPS_8.0.30		:= $(GLIBC_IMAGE)	$(GCC_IMAGE)	1.3.1	8.11.1		16.6	1.11.3	2.8.4
DEPS_8.1.31		:= $(GLIBC_IMAGE)	$(GCC_IMAGE)	1.3.1	8.11.1		16.6	1.11.3	2.8.4
DEPS_8.2.27		:= $(GLIBC_IMAGE)	$(GCC_IMAGE)	1.3.1	8.11.1		16.6	1.11.3	2.8.4
DEPS_8.3.16		:= $(GLIBC_IMAGE)	$(GCC_IMAGE)	1.3.1	8.11.1		16.6	1.11.3	2.8.4
DEPS_8.4.3		:= $(GLIBC_IMAGE)	$(GCC_IMAGE)	1.3.1	8.11.1		16.6	1.11.3	2.8.4

# базовое название для образа
IMAGE			:= dmp/php
FIRST_VERSION	:= $(firstword $(VERSION_CHAIN))
NO_CACHE_TEXT	:= $(if $(filter 1,$(NO_CACHE)),--no-cache,)


# helper: поиск предыдущей версии в VERSION_CHAIN
prev_of = $(shell echo "$(VERSION_CHAIN)" | awk -v v='$(1)' '{for(i=1;i<=NF;i++){if($$i==v){print p;exit};p=$$i}}')


.PHONY: help
help:
	@echo "make:"
	@echo "  make all        собрать всё"
	@echo "  make sources    скачать исходники (и composer) для цепочки вплоть до $(VERSION)"
	@echo "  make dist       собрать цепочку -dist образов одну за другой до $(VERSION)"
	@echo "  make build      собрать образ $(IMAGE)"
	@echo "  make test       проверить образ: php, расширения (openssl/curl/pdo/gd/intl...), composer"
	@echo ""
	@echo "  Цепочка: $(VERSION_CHAIN)"
	@echo "  Каждая версия собирается тулчейном dmp/gcc, рантайм — dmp/glibc."
	@echo "  Каждый образ тегируется полной и короткой версией (напр. 8.3.16 и 8.3)."
	@echo "  Dev-зависимости задаются в APT_DEPS, рантайм .so — в RUNTIME_APT_DEPS."
	@echo "  zlib/curl/libpq/libzip собираются из исходников (стоковые версии Astra малы для php 8.x);"
	@echo "  их версии и версия composer — в DEPS_<версия> (libzip требует cmake из APT_DEPS)."
	@echo "  Nexus: composer-config.json.example; CA: php.ini.example (openssl.cafile/curl.cainfo)."
	@echo "  Другая версия: make all VERSION=8.3.16"
	@echo ""


.PHONY: debug
debug:
	$(info BASE_DIR         = $(BASE_DIR))
	$(info ARTIFACTS        = $(ARTIFACTS))
	$(info BUILD_CONTEXT    = $(BUILD_CONTEXT))
	$(info TEST_DIR         = $(TEST_DIR))
	$(info VERSION_CHAIN    = $(VERSION_CHAIN))
	$(info VERSION          = $(VERSION))
	$(info FIRST_VERSION    = $(FIRST_VERSION))
	$(info BASE_URL         = $(BASE_URL))
	$(info ZLIB_URL         = $(ZLIB_URL))
	$(info CURL_URL         = $(CURL_URL))
	$(info LIBPQ_URL        = $(LIBPQ_URL))
	$(info ZIP_URL          = $(ZIP_URL))
	$(info COMPOSER_URL     = $(COMPOSER_URL))
	$(info NO_CACHE         = $(NO_CACHE))
	$(info NO_CACHE_TEXT    = $(NO_CACHE_TEXT))
	$(info APT_DEPS         = $(APT_DEPS))
	$(info RUNTIME_APT_DEPS = $(RUNTIME_APT_DEPS))
	$(info IMAGE            = $(IMAGE):$(VERSION))
	$(info DEPS($(VERSION)) = $(DEPS_$(VERSION)))
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
	zlib=$$3; curl=$$4; libpq=$$5; zip=$$6; composer=$$7
	mkdir -p $(ARTIFACTS)/src
	# полные url собираются из базового url и версии (версии — в DEPS_<версия>)
	[ -f "$(ARTIFACTS)/src/php-$$current.tar.xz" ] || curl -v -L -o "$(ARTIFACTS)/src/php-$$current.tar.xz" "$(BASE_URL)/php-$$current.tar.xz"
	[ -f "$(ARTIFACTS)/src/zlib-$$zlib.tar.gz" ] || curl -v -L -o "$(ARTIFACTS)/src/zlib-$$zlib.tar.gz" "$(ZLIB_URL)/v$$zlib/zlib-$$zlib.tar.gz"
	[ -f "$(ARTIFACTS)/src/curl-$$curl.tar.xz" ] || curl -v -L -o "$(ARTIFACTS)/src/curl-$$curl.tar.xz" "$(CURL_URL)/curl-$$curl.tar.xz"
	[ -f "$(ARTIFACTS)/src/postgresql-$$libpq.tar.bz2" ] || curl -v -L -o "$(ARTIFACTS)/src/postgresql-$$libpq.tar.bz2" "$(LIBPQ_URL)/v$$libpq/postgresql-$$libpq.tar.bz2"
	[ -f "$(ARTIFACTS)/src/libzip-$$zip.tar.xz" ] || curl -v -L -o "$(ARTIFACTS)/src/libzip-$$zip.tar.xz" "$(ZIP_URL)/libzip-$$zip.tar.xz"
	[ -f "$(ARTIFACTS)/composer-$$composer.phar" ] || curl -v -L -o "$(ARTIFACTS)/composer-$$composer.phar" "$(COMPOSER_URL)/$$composer/composer.phar"


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
	runtime=$$1; build=$$2; zlib=$$3; curl=$$4; libpq=$$5; zip=$$6
	short=$${current%.*}
	docker build -f Dockerfile $(NO_CACHE_TEXT) --target dist \
		--build-arg RUNTIME_IMAGE=$$runtime 	\
		--build-arg BUILD_IMAGE=$$build 		\
		--build-arg PHP_VERSION=$$current 		\
		--build-arg ZLIB_VERSION=$$zlib 		\
		--build-arg CURL_VERSION=$$curl 		\
		--build-arg LIBPQ_VERSION=$$libpq 		\
		--build-arg ZIP_VERSION=$$zip 			\
		--build-arg APT_DEPS="$(APT_DEPS)" 		\
		--tag $(IMAGE):$$current-dist 			\
		--tag $(IMAGE):$$short-dist 			\
		--progress plain $(BUILD_CONTEXT)


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
	runtime=$$1; build=$$2; zlib=$$3; curl=$$4; libpq=$$5; zip=$$6; composer=$$7
	short=$${current%.*}
	docker build -f Dockerfile $(NO_CACHE_TEXT) 			\
		--build-arg RUNTIME_IMAGE=$$runtime 				\
		--build-arg BUILD_IMAGE=$$build 					\
		--build-arg PHP_VERSION=$$current 					\
		--build-arg ZLIB_VERSION=$$zlib 					\
		--build-arg CURL_VERSION=$$curl 					\
		--build-arg LIBPQ_VERSION=$$libpq 					\
		--build-arg ZIP_VERSION=$$zip 						\
		--build-arg COMPOSER_VERSION=$$composer 			\
		--build-arg APT_DEPS="$(APT_DEPS)" 					\
		--build-arg RUNTIME_APT_DEPS="$(RUNTIME_APT_DEPS)" 	\
		--tag $(IMAGE):$$current 							\
		--tag $(IMAGE):$$short 								\
		--progress plain $(BUILD_CONTEXT)


# тестирование собранного образа
.PHONY: test
test: test-$(VERSION)

.ONESHELL:
test-%: build-%
	@current=$(*)
	docker run --rm --entrypoint /bin/bash -v $(TEST_DIR):/test:ro $(IMAGE):$$current -euc '\
		echo "== versions =="; 					\
		php --version; 							\
		command -v php; command -v composer; 	\
		echo "== run hello.php =="; 			\
		php /test/hello.php; 					\
		echo "== functional checks =="; 		\
		php /test/phpcheck.php; 				\
		echo "== composer =="; 					\
		composer --version; 					\
		cp -r /test/demo /tmp/demo && cd /tmp/demo && composer validate --no-check-publish'


.PHONY: clean
clean: clean-$(VERSION)

.ONESHELL:
clean-%:
	@current=$(*)
	@prev='$(call prev_of,$*)'
	# вызываем рекурсивно предыдущую версию
	[ -n "$$prev" ] && $(MAKE) clean-$$prev || true
	set -e
	set -- $(DEPS_$*)
	zlib=$$3; curl=$$4; libpq=$$5; zip=$$6; composer=$$7
	rm -f "$(ARTIFACTS)/src/php-$$current.tar.xz" \
		"$(ARTIFACTS)/src/zlib-$$zlib.tar.gz" \
		"$(ARTIFACTS)/src/curl-$$curl.tar.xz" \
		"$(ARTIFACTS)/src/postgresql-$$libpq.tar.bz2" \
		"$(ARTIFACTS)/src/libzip-$$zip.tar.xz" \
		"$(ARTIFACTS)/composer-$$composer.phar"


.PHONY: all
all: sources dist build test
