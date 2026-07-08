BASE_DIR      	?= $(abspath .)
ARTIFACTS     	?= $(BASE_DIR)/artifacts
BUILD_CONTEXT 	?= $(BASE_DIR)
TEST_DIR		?= $(BASE_DIR)/test


.DEFAULT_GOAL := help

# список версий, которые поддерживает сборщик
# версии fluent-bit независимы, но собираются одна за другой, строго по цепочке
VERSION_CHAIN		?= 3.2.10 4.0.14 4.2.6 5.0.9
# целевая версия сборки. по умолчанию берется последняя версия
VERSION      		?= $(lastword $(VERSION_CHAIN))
# базовые url адреса (конкретные версии подставляются в цели sources)
# fluent-bit (github archive: .../archive/refs/tags/v<версия>.tar.gz)
BASE_URL			?= https://github.com/fluent/fluent-bit/archive/refs/tags
# openssl (releases/download/openssl-<версия>/openssl-<версия>.tar.gz)
OPENSSL_URL			?= https://github.com/openssl/openssl/releases/download
# libyaml (releases/download/<версия>/yaml-<версия>.tar.gz)
LIBYAML_URL			?= https://github.com/yaml/libyaml/releases/download
# docker build --no-cache
NO_CACHE			?= 0
# apt-deps (build): flex/bison/m4 нужны генератору парсеров, pkg-config — cmake-поиску
APT_DEPS			?= flex bison m4 pkg-config

GLIBC_IMAGE			?= dmp/glibc:2.28
GCC_IMAGE			?= dmp/gcc:14.2.0-dist
CMAKE_IMAGE			?= dmp/cmake:3.27.9-dist

# зависимости для сборки разных версии
# openssl/libyaml собираются из исходников и линкуются СТАТИЧЕСКИ
# версия      runtime_image    build_image     cmake_image      openssl   libyaml
DEPS_3.2.10	:= $(GLIBC_IMAGE)   $(GCC_IMAGE)    $(CMAKE_IMAGE)   3.0.16    0.2.5
DEPS_4.0.14	:= $(GLIBC_IMAGE)   $(GCC_IMAGE)    $(CMAKE_IMAGE)   3.0.16    0.2.5
DEPS_4.2.6	:= $(GLIBC_IMAGE)   $(GCC_IMAGE)    $(CMAKE_IMAGE)   3.0.16    0.2.5
DEPS_5.0.9	:= $(GLIBC_IMAGE)   $(GCC_IMAGE)    $(CMAKE_IMAGE)   3.0.16    0.2.5

# базовое название для образа
IMAGE			:= dmp/fluent-bit
FIRST_VERSION	:= $(firstword $(VERSION_CHAIN))
NO_CACHE_TEXT	:= $(if $(filter 1,$(NO_CACHE)),--no-cache,)


# helper: поиск предыдущей версии в VERSION_CHAIN
prev_of = $(shell echo "$(VERSION_CHAIN)" | awk -v v='$(1)' '{for(i=1;i<=NF;i++){if($$i==v){print p;exit};p=$$i}}')


.PHONY: help
help:
	@echo "make:"
	@echo "  make all        собрать всё"
	@echo "  make sources    скачать исходники (fluent-bit/openssl/libyaml) для цепочки вплоть до $(VERSION)"
	@echo "  make dist       собрать цепочку -dist образов одну за другой до $(VERSION)"
	@echo "  make build      собрать образ $(IMAGE)"
	@echo "  make test       проверить образ: fluent-bit --version, список плагинов, smoke-пайплайн"
	@echo ""
	@echo "  Цепочка: $(VERSION_CHAIN)"
	@echo "  Каждая версия собирается тулчейном dmp/gcc, cmake берётся из dmp/cmake, рантайм — чистый dmp/glibc."
	@echo "  Максимум модулей: FLB_ALL + FLB_JEMALLOC (56 in / 49 out / 24 filter / 9 processor)."
	@echo "  OpenSSL/libyaml собираются из исходников и линкуются СТАТИЧЕСКИ — runtime-.so не требуются."
	@echo "  libstdc++/libgcc линкуются статически — рантайму нужен только glibc."
	@echo "  Каждый образ тегируется полной и короткой версией (напр. 5.0.9 и 5.0)."
	@echo "  Версии зависимостей — в DEPS_<версия>."
	@echo "  Другая версия: make all VERSION=4.2.6"
	@echo "  Только одна версия (без цепочки): make build VERSION=5.0.9 VERSION_CHAIN=5.0.9"
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
	$(info OPENSSL_URL    = $(OPENSSL_URL))
	$(info LIBYAML_URL    = $(LIBYAML_URL))
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
	set -e
	# вызываем рекурсивно предыдущую версию
	if [ -n "$$prev" ]; then $(MAKE) sources-$$prev; fi
	set -- $(DEPS_$*)
	openssl=$$4; libyaml=$$5
	mkdir -p $(ARTIFACTS)/src
	# полные url собираются из базового url и версии (версии — в DEPS_<версия>)
	curl -v -L --skip-existing -o "$(ARTIFACTS)/src/fluent-bit-$$current.tar.gz" "$(BASE_URL)/v$$current.tar.gz"
	curl -v -L --skip-existing -o "$(ARTIFACTS)/src/openssl-$$openssl.tar.gz" "$(OPENSSL_URL)/openssl-$$openssl/openssl-$$openssl.tar.gz"
	curl -v -L --skip-existing -o "$(ARTIFACTS)/src/yaml-$$libyaml.tar.gz" "$(LIBYAML_URL)/$$libyaml/yaml-$$libyaml.tar.gz"


# docker build --target dist - дистрибутив со скомпилированными исходниками (без установки в систему)
.PHONY: dist
dist: dist-$(VERSION)

.ONESHELL:
dist-%: sources-%
	@current=$(*)
	prev='$(call prev_of,$*)'
	set -e
	# рекурсивно собираем предыдущую версию
	if [ -n "$$prev" ]; then $(MAKE) dist-$$prev; fi
	set -- $(DEPS_$*)
	runtime=$$1; build=$$2; cmake=$$3; openssl=$$4; libyaml=$$5
	short=$${current%.*}
	docker build -f Dockerfile $(NO_CACHE_TEXT) --target dist \
		--build-arg RUNTIME_IMAGE=$$runtime 		\
		--build-arg BUILD_IMAGE=$$build 			\
		--build-arg CMAKE_IMAGE=$$cmake 			\
		--build-arg FLB_VERSION=$$current 			\
		--build-arg OPENSSL_VERSION=$$openssl 		\
		--build-arg LIBYAML_VERSION=$$libyaml 		\
		--build-arg APT_DEPS="$(APT_DEPS)" 			\
		--tag $(IMAGE):$$current-dist 				\
		--tag $(IMAGE):$$short-dist 				\
		--progress plain $(BUILD_CONTEXT)


# docker build - итоговый образ с установленным ПО (установка из дистрибутива)
.PHONY: build
build: build-$(VERSION)

.ONESHELL:
build-%: dist-%
	@current=$(*)
	prev='$(call prev_of,$*)'
	set -e
	# рекурсивно собираем предыдущую версию
	if [ -n "$$prev" ]; then $(MAKE) build-$$prev; fi
	set -- $(DEPS_$*)
	runtime=$$1; build=$$2; cmake=$$3; openssl=$$4; libyaml=$$5
	short=$${current%.*}
	docker build -f Dockerfile $(NO_CACHE_TEXT) 	\
		--build-arg RUNTIME_IMAGE=$$runtime 		\
		--build-arg BUILD_IMAGE=$$build 			\
		--build-arg CMAKE_IMAGE=$$cmake 			\
		--build-arg FLB_VERSION=$$current 			\
		--build-arg OPENSSL_VERSION=$$openssl 		\
		--build-arg LIBYAML_VERSION=$$libyaml 		\
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
		fluent-bit --version; 					\
		echo "== build info =="; 				\
		fluent-bit --help 2>&1 | head -1; 		\
		echo "== functional check =="; 			\
		/test/fluentbitcheck.sh'


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
	openssl=$$4; libyaml=$$5
	rm -f "$(ARTIFACTS)/src/fluent-bit-$$current.tar.gz" \
		"$(ARTIFACTS)/src/openssl-$$openssl.tar.gz" \
		"$(ARTIFACTS)/src/yaml-$$libyaml.tar.gz"


.PHONY: all
all: sources dist build test
