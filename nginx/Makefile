BASE_DIR      	?= $(abspath .)
ARTIFACTS     	?= $(BASE_DIR)/artifacts
BUILD_CONTEXT 	?= $(BASE_DIR)
TEST_DIR		?= $(BASE_DIR)/test


.DEFAULT_GOAL := help

# список версий, которые поддерживает сборщик
# версии nginx независимы, но собираются одна за другой, строго по цепочке
VERSION_CHAIN		?= 1.24.0 1.26.3 1.27.4 1.28.0
# целевая версия сборки. по умолчанию берется последняя версия
VERSION      		?= $(lastword $(VERSION_CHAIN))
# базовые url адреса (конкретные версии подставляются в цели sources)
# nginx
BASE_URL			?= https://nginx.org/download
# pcre2
PCRE2_URL			?= https://github.com/PCRE2Project/pcre2/releases/download
# zlib
ZLIB_URL			?= https://github.com/madler/zlib/releases/download
# openssl
OPENSSL_URL			?= https://github.com/openssl/openssl/releases/download
# docker build --no-cache
NO_CACHE			?= 0
# apt-deps (build): perl нужен для OpenSSL Configure
APT_DEPS			?= perl

GLIBC_IMAGE			?= dmp/glibc:2.28
GCC_IMAGE			?= dmp/gcc:8.5.0-dist

# зависимости для сборки разных версии
# http3: 0 | 1 — модуль http_v3 (QUIC) доступен только с nginx >= 1.25
# версия           runtime_image	build_image		pcre2	zlib	openssl		http3
DEPS_1.24.0		:= $(GLIBC_IMAGE)	$(GCC_IMAGE)	10.45	1.3.1	3.0.16		0
DEPS_1.26.3		:= $(GLIBC_IMAGE)	$(GCC_IMAGE)	10.45	1.3.1	3.0.16		1
DEPS_1.27.4		:= $(GLIBC_IMAGE)	$(GCC_IMAGE)	10.45	1.3.1	3.0.16		1
DEPS_1.28.0		:= $(GLIBC_IMAGE)	$(GCC_IMAGE)	10.45	1.3.1	3.0.16		1

# базовое название для образа
IMAGE			:= dmp/nginx
FIRST_VERSION	:= $(firstword $(VERSION_CHAIN))
NO_CACHE_TEXT	:= $(if $(filter 1,$(NO_CACHE)),--no-cache,)


# helper: поиск предыдущей версии в VERSION_CHAIN
prev_of = $(shell echo "$(VERSION_CHAIN)" | awk -v v='$(1)' '{for(i=1;i<=NF;i++){if($$i==v){print p;exit};p=$$i}}')


.PHONY: help
help:
	@echo "make:"
	@echo "  make all        собрать всё"
	@echo "  make sources    скачать исходники (nginx/pcre2/zlib/openssl) для цепочки вплоть до $(VERSION)"
	@echo "  make dist       собрать цепочку -dist образов одну за другой до $(VERSION)"
	@echo "  make build      собрать образ $(IMAGE)"
	@echo "  make test       проверить образ: nginx -V, -t, модули, запуск и HTTP-ответ"
	@echo ""
	@echo "  Цепочка: $(VERSION_CHAIN)"
	@echo "  Каждая версия собирается тулчейном dmp/gcc, рантайм — чистый dmp/glibc."
	@echo "  PCRE2/zlib/OpenSSL линкуются СТАТИЧЕСКИ — runtime-.so не требуются."
	@echo "  Каждый образ тегируется полной и короткой версией (напр. 1.28.0 и 1.28)."
	@echo "  Версии зависимостей и флаг http3 — в DEPS_<версия> (http_v3 только nginx >= 1.25)."
	@echo "  Свой конфиг: nginx.conf.example -> nginx.conf."
	@echo "  Другая версия: make all VERSION=1.26.3"
	@echo ""


.PHONY: debug
debug:
	$(info BASE_DIR       = $(BASE_DIR))
	$(info ARTIFACTS      = $(ARTIFACTS))
	$(info BUILD_CONTEXT  = $(BUILD_CONTEXT))
	$(info TEST_DIR       = $(TEST_DIR))
	$(info VERSION_CHAIN  = $(VERSION_CHAIN))
	$(info VERSION        = $(VERSION))
	$(info FIRST_VERSION  = $(FIRST_VERSION))
	$(info BASE_URL       = $(BASE_URL))
	$(info PCRE2_URL      = $(PCRE2_URL))
	$(info ZLIB_URL       = $(ZLIB_URL))
	$(info OPENSSL_URL    = $(OPENSSL_URL))
	$(info NO_CACHE       = $(NO_CACHE))
	$(info NO_CACHE_TEXT  = $(NO_CACHE_TEXT))
	$(info APT_DEPS       = $(APT_DEPS))
	$(info IMAGE          = $(IMAGE):$(VERSION))
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
	pcre2=$$3; zlib=$$4; openssl=$$5
	mkdir -p $(ARTIFACTS)/src
	# полные url собираются из базового url и версии (версии — в DEPS_<версия>)
	curl -v -L --skip-existing -o "$(ARTIFACTS)/src/nginx-$$current.tar.gz" "$(BASE_URL)/nginx-$$current.tar.gz"
	curl -v -L --skip-existing -o "$(ARTIFACTS)/src/pcre2-$$pcre2.tar.gz" "$(PCRE2_URL)/pcre2-$$pcre2/pcre2-$$pcre2.tar.gz"
	curl -v -L --skip-existing -o "$(ARTIFACTS)/src/zlib-$$zlib.tar.gz" "$(ZLIB_URL)/v$$zlib/zlib-$$zlib.tar.gz"
	curl -v -L --skip-existing -o "$(ARTIFACTS)/src/openssl-$$openssl.tar.gz" "$(OPENSSL_URL)/openssl-$$openssl/openssl-$$openssl.tar.gz"


# docker build --target dist - дистрибутив со скомпилированными исходниками (без установки в систему)
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
	runtime=$$1; build=$$2; pcre2=$$3; zlib=$$4; openssl=$$5; http3=$$6
	short=$${current%.*}
	docker build -f Dockerfile $(NO_CACHE_TEXT) --target dist \
		--build-arg RUNTIME_IMAGE=$$runtime 	\
		--build-arg BUILD_IMAGE=$$build 		\
		--build-arg NGINX_VERSION=$$current 	\
		--build-arg PCRE2_VERSION=$$pcre2 		\
		--build-arg ZLIB_VERSION=$$zlib 		\
		--build-arg OPENSSL_VERSION=$$openssl 	\
		--build-arg NGINX_HTTP3=$$http3 		\
		--build-arg APT_DEPS="$(APT_DEPS)" 		\
		--tag $(IMAGE):$$current-dist 			\
		--tag $(IMAGE):$$short-dist 			\
		--progress plain $(BUILD_CONTEXT)


# docker build - итоговый образ с установленным ПО (установка из дистрибутива)
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
	runtime=$$1; build=$$2; pcre2=$$3; zlib=$$4; openssl=$$5; http3=$$6
	short=$${current%.*}
	docker build -f Dockerfile $(NO_CACHE_TEXT) 	\
		--build-arg RUNTIME_IMAGE=$$runtime 		\
		--build-arg BUILD_IMAGE=$$build 			\
		--build-arg NGINX_VERSION=$$current 		\
		--build-arg PCRE2_VERSION=$$pcre2 			\
		--build-arg ZLIB_VERSION=$$zlib 			\
		--build-arg OPENSSL_VERSION=$$openssl 		\
		--build-arg NGINX_HTTP3=$$http3 			\
		--build-arg APT_DEPS="$(APT_DEPS)" 			\
		--tag $(IMAGE):$$current 					\
		--tag $(IMAGE):$$short 						\
		--progress plain $(BUILD_CONTEXT)


# тестирование собранного образа
.PHONY: test
test: test-$(VERSION)

.ONESHELL:
test-%: build-%
	@current=$(*)
	docker run --rm --entrypoint /bin/bash -v $(TEST_DIR):/test:ro $(IMAGE):$$current -euc '\
		echo "== version =="; 					\
		nginx -v; 								\
		echo "== configure (-V) =="; 			\
		nginx -V; 								\
		echo "== config test (-t) =="; 			\
		nginx -t; 								\
		echo "== functional check =="; 			\
		/test/nginxcheck.sh'


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
	pcre2=$$3; zlib=$$4; openssl=$$5
	rm -f "$(ARTIFACTS)/src/nginx-$$current.tar.gz" \
		"$(ARTIFACTS)/src/pcre2-$$pcre2.tar.gz" \
		"$(ARTIFACTS)/src/zlib-$$zlib.tar.gz" \
		"$(ARTIFACTS)/src/openssl-$$openssl.tar.gz"


.PHONY: all
all: sources dist build test
