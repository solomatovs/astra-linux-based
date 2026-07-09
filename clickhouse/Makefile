BASE_DIR      	?= $(abspath .)
ARTIFACTS     	?= $(BASE_DIR)/artifacts
BUILD_CONTEXT 	?= $(BASE_DIR)
TEST_DIR		?= $(BASE_DIR)/test


.DEFAULT_GOAL := help

# список версий, которые поддерживает сборщик
# версии собираются одна за другой, строго по цепочке
VERSION_CHAIN		?= 22.12.6.22-stable 23.12.6.19-stable 24.12.6.70-stable 25.12.12.1-stable 26.7.1.1-new
# целевая версия сборки. по умолчанию берется последняя версия
# соответственно для ее сборки потребуется собрать все предыдущие версии в цепочки
VERSION      		?= $(lastword $(VERSION_CHAIN))
# базовый url адрес для скачивания исходников собираемых в Dockerfile
BASE_URL			?= https://github.com/ClickHouse/ClickHouse.git
NINJA_URL			?= https://github.com/ninja-build/ninja/archive/refs/tags
# выполнять ли сброс кэша: docker build --no-cache
NO_CACHE			?= 0

GLIBC_IMAGE			?= dmp/glibc:2.28
GCC_IMAGE			?= dmp/gcc:12.4.0-dist
CMAKE_IMAGE			?= dmp/cmake:3.27.9-dist
PYTHON_IMAGE		?= dmp/python:3.10-dist
LLVM15_IMAGE		?= dmp/llvm:15.0.7-dist
LLVM17_IMAGE		?= dmp/llvm:17.0.6-dist
LLVM18_IMAGE		?= dmp/llvm:18.1.8-dist
LLVM20_IMAGE		?= dmp/llvm:20.1.8-dist
LLVM21_IMAGE		?= dmp/llvm:21.1.8-dist

# зависимости для сборки разных версии
# cc:  clang | gcc  — ClickHouse собирается только clang (gcc официально не поддерживается)
# каждому мажору нужен свой clang: 22->15, 23->17, 24->18, 25->20, 26->21
# версия                 	runtime_image    build_image      cmake_image      python_image     gcc_image       llvm_image        cc      ninja
DEPS_22.12.6.22-stable	:= 	$(GLIBC_IMAGE)   $(GLIBC_IMAGE)   $(CMAKE_IMAGE)   $(PYTHON_IMAGE)  $(GCC_IMAGE)    $(LLVM15_IMAGE)   clang   1.11.1
DEPS_23.12.6.19-stable	:= 	$(GLIBC_IMAGE)   $(GLIBC_IMAGE)   $(CMAKE_IMAGE)   $(PYTHON_IMAGE)  $(GCC_IMAGE)    $(LLVM17_IMAGE)   clang   1.11.1
DEPS_24.12.6.70-stable	:= 	$(GLIBC_IMAGE)   $(GLIBC_IMAGE)   $(CMAKE_IMAGE)   $(PYTHON_IMAGE)  $(GCC_IMAGE)    $(LLVM18_IMAGE)   clang   1.11.1
DEPS_25.12.12.1-stable	:= 	$(GLIBC_IMAGE)   $(GLIBC_IMAGE)   $(CMAKE_IMAGE)   $(PYTHON_IMAGE)  $(GCC_IMAGE)    $(LLVM20_IMAGE)   clang   1.11.1
DEPS_26.7.1.1-new		:= 	$(GLIBC_IMAGE)   $(GLIBC_IMAGE)   $(CMAKE_IMAGE)   $(PYTHON_IMAGE)  $(GCC_IMAGE)    $(LLVM21_IMAGE)   clang   1.11.1

# базовое название для образа
IMAGE			:= dmp/clickhouse
FIRST_VERSION	:= $(firstword $(VERSION_CHAIN))
NO_CACHE_TEXT	:= $(if $(filter 1,$(NO_CACHE)),--no-cache,)


# helper: поиск предыдущей версии в VERSION_CHAIN
prev_of = $(shell echo "$(VERSION_CHAIN)" | awk -v v='$(1)' '{for(i=1;i<=NF;i++){if($$i==v){print p;exit};p=$$i}}')


.PHONY: help
help:
	@echo "make:"
	@echo "  make all        собрать всё"
	@echo "  make sources    скачать исходники для цепочки вплоть до $(VERSION)"
	@echo "  make dist       собрать цепочку -dist образов одну за другой до $(VERSION)"
	@echo "  make build      собрать образ $(IMAGE)"
	@echo "  make test       проверить образ $(IMAGE): clickhouse --version, server/client/local"
	@echo ""
	@echo "  Цепочка: $(VERSION_CHAIN)"
	@echo "  Зависимости (runtime_image/build_image/cmake_image/python_image/gcc_image/llvm_image/cc/ninja) задаются пословно в DEPS_<версия>."
	@echo "  Другая версия: make all VERSION=25.12.12.1-stable"
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
	$(info NINJA_URL      = $(NINJA_URL))
	$(info NO_CACHE       = $(NO_CACHE))
	$(info NO_CACHE_TEXT  = $(NO_CACHE_TEXT))
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
	mkdir -p $(ARTIFACTS)/src
	if [ ! -f "$(ARTIFACTS)/src/clickhouse-$$current.tar.gz" ]; then		\
		rm -rf "$(ARTIFACTS)/src/ClickHouse-$$current";						\
		git clone --branch "v$$current" --depth 1							\
			--recurse-submodules --shallow-submodules						\
			"$(BASE_URL)" "$(ARTIFACTS)/src/ClickHouse-$$current";			\
		tar czf "$(ARTIFACTS)/src/clickhouse-$$current.tar.gz"				\
			-C "$(ARTIFACTS)/src" --exclude='.git' "ClickHouse-$$current";	\
		rm -rf "$(ARTIFACTS)/src/ClickHouse-$$current";						\
	fi
	[ -f "$(ARTIFACTS)/src/ninja-$$8.tar.gz" ] || curl -v -L -o "$(ARTIFACTS)/src/ninja-$$8.tar.gz" "$(NINJA_URL)/v$$8.tar.gz"


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
	docker build -f Dockerfile $(NO_CACHE_TEXT) --target dist \
		--build-arg RUNTIME_IMAGE=$$1					\
		--build-arg BUILD_IMAGE=$$2 					\
		--build-arg CMAKE_IMAGE=$$3 					\
		--build-arg PYTHON_IMAGE=$$4 					\
		--build-arg GCC_IMAGE=$$5 						\
		--build-arg LLVM_IMAGE=$$6 						\
		--build-arg CLICKHOUSE_CC=$$7 					\
		--build-arg CLICKHOUSE_VERSION=$$current 		\
		--build-arg NINJA_VERSION=$$8 					\
		--tag $(IMAGE):$$current-dist 					\
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
	docker build -f Dockerfile $(NO_CACHE_TEXT)	\
		--build-arg RUNTIME_IMAGE=$$1			\
		--build-arg BUILD_IMAGE=$$2				\
		--build-arg CMAKE_IMAGE=$$3				\
		--build-arg PYTHON_IMAGE=$$4			\
		--build-arg GCC_IMAGE=$$5				\
		--build-arg LLVM_IMAGE=$$6				\
		--build-arg CLICKHOUSE_CC=$$7			\
		--build-arg CLICKHOUSE_VERSION=$$current\
		--build-arg NINJA_VERSION=$$8			\
		--tag $(IMAGE):$$current				\
		--progress plain $(BUILD_CONTEXT)


# тестирование собранного образа
.PHONY: test
test: test-$(VERSION)

.ONESHELL:
test-%: build-%
	@current=$(*)
	docker run --rm --entrypoint /bin/bash -v $(TEST_DIR):/test:ro $(IMAGE):$$current -euc '\
		echo "== version ==";					\
		clickhouse local --version;				\
		echo "== наличие компонентов =="; 		\
		for t in server client local keeper compressor; do 	\
			clickhouse $$t --help >/dev/null 2>&1 && echo "  ok   clickhouse-$$t" || echo "  MISS clickhouse-$$t"; 	\
		done;									\
		echo "== smoke ==";						\
		clickhouse local --query "$$(cat /test/smoke.sql)"'


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
	rm -f "$(ARTIFACTS)/src/clickhouse-$$current.tar.gz" \
	      "$(ARTIFACTS)/src/ninja-$$8.tar.gz"


.PHONY: all
all: sources dist build test
