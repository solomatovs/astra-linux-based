BASE_DIR      	?= $(abspath .)
ARTIFACTS     	?= $(abspath $(BASE_DIR)/../../ci-artifacts/clickhouse)
BUILD_CONTEXT 	?= $(BASE_DIR)
# рабочая копия $(ARTIFACTS) внутри контекста сборки (хардлинки, см. artifacts-link)
LINKED_ARTIFACTS ?= $(BUILD_CONTEXT)/artifacts
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


# FORCE=1 — пересобрать, даже если образ уже есть
FORCE				?= 0

# уже собранные образы, снимок на момент запуска make
BUILT				:= $(if $(filter 1,$(FORCE) $(NO_CACHE)),,$(shell docker images --format '{{.Repository}}:{{.Tag}}' 2>/dev/null))
built = $(filter $(1),$(BUILT))

# зависимости версии берутся из DEPS_<версия>: ссылка на образ этого же проекта
# означает, что сначала надо собрать то звено
deps_of = $(patsubst $(IMAGE):%-dist,dist-%,$(filter $(IMAGE):%-dist,$(DEPS_$(1)))) \
          $(patsubst $(IMAGE):%,build-%,$(filter-out %-dist,$(filter $(IMAGE):%,$(DEPS_$(1)))))

# что собрать перед версией; если образ уже есть — ничего
need_dist  = $(if $(call built,$(IMAGE):$(1)-dist),,sources-$(1) $(call deps_of,$(1)) artifacts-link)
need_build = $(if $(call built,$(IMAGE):$(1)),,dist-$(1) $(call deps_of,$(1)) artifacts-link)

# второй проход по списку зависимостей: только там уже известен $* (версия),
# поэтому $$(call need_dist,$$*) можно вычислить
.SECONDEXPANSION:
# зависимости обновляются слева направо, artifacts-link должен отработать последним
.NOTPARALLEL:

.PHONY: sources-all dist-all build-all test-all clean-all
sources-all:		$(addprefix sources-,$(VERSION_CHAIN))
dist-all:		$(addprefix dist-,$(VERSION_CHAIN))
build-all:		$(addprefix build-,$(VERSION_CHAIN))
test-all:		$(addprefix test-,$(VERSION_CHAIN))
clean-all:		$(addprefix clean-,$(VERSION_CHAIN))


.PHONY: help
help:
	@echo "make:"
	@echo "  make all        собрать всё"
	@echo "  make sources    скачать исходники для $(VERSION)"
	@echo "  make dist       собрать -dist образ $(VERSION) (звенья цепочки — если нужны)"
	@echo "  make build      собрать образ $(IMAGE)"
	@echo "  make test       проверить образ $(IMAGE): clickhouse --version, server/client/local"
	@echo ""
	@echo "  Цепочка: $(VERSION_CHAIN)"
	@echo "  Зависимости (runtime_image/build_image/cmake_image/python_image/gcc_image/llvm_image/cc/ninja) задаются пословно в DEPS_<версия>."
	@echo "  Другая версия: make all VERSION=25.12.12.1-stable"
	@echo ""
	@echo "  make <стадия>-all  то же для всех версий из VERSION_CHAIN"
	@echo "  FORCE=1            пересобрать, даже если образ уже есть"


.PHONY: debug
debug:
	$(info BASE_DIR       = $(BASE_DIR))
	$(info ARTIFACTS      = $(ARTIFACTS))
	$(info BUILD_CONTEXT  = $(BUILD_CONTEXT))
	$(info LINKED_ARTIFACTS = $(LINKED_ARTIFACTS))
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


# docker build видит только свой контекст, поэтому перед сборкой подкладываем
# $(ARTIFACTS) в $(LINKED_ARTIFACTS) хардлинками: места не занимает, копий нет
.PHONY: artifacts-link
artifacts-link:
	@set -e
	if [ ! -d "$(ARTIFACTS)" ]; then
		if [ -n "$$(ls -A '$(LINKED_ARTIFACTS)' 2>/dev/null)" ]; then
			echo "  ! $(ARTIFACTS) не найден, а $(LINKED_ARTIFACTS) не пуст" >&2
			echo "  ! перенеси артефакты в ci-artifacts, тут только рабочая копия" >&2
			exit 1
		fi
		mkdir -p "$(ARTIFACTS)"
	fi
	rm -rf "$(LINKED_ARTIFACTS)"
	cp -al "$(ARTIFACTS)" "$(LINKED_ARTIFACTS)" 2>/dev/null || {
		rm -rf "$(LINKED_ARTIFACTS)"
		cp -a "$(ARTIFACTS)" "$(LINKED_ARTIFACTS)"
	}


# Скачиваем зависимости для сборки дистрибутива
.PHONY: sources
sources: sources-$(VERSION)

.ONESHELL:
sources-%:
	@current=$(*)
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
dist-%: $$(call need_dist,$$*)
	@current=$(*)
	set -e
	set -- $(DEPS_$*)
	[ -z "$(call built,$(IMAGE):$*-dist)" ] || { echo "  = $(IMAGE):$*-dist уже собран (FORCE=1 — пересобрать)"; exit 0; }
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
build-%: $$(call need_build,$$*)
	@current=$(*)
	set -e
	set -- $(DEPS_$*)
	[ -z "$(call built,$(IMAGE):$*)" ] || { echo "  = $(IMAGE):$* уже собран (FORCE=1 — пересобрать)"; exit 0; }
	docker build -f Dockerfile $(NO_CACHE_TEXT)	\
		--build-arg DIST_IMAGE=$(IMAGE):$*-dist	\
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
	rm -rf "$(LINKED_ARTIFACTS)"

.ONESHELL:
clean-%:
	@current=$(*)
	set -e
	set -- $(DEPS_$*)
	rm -f "$(ARTIFACTS)/src/clickhouse-$$current.tar.gz" \
	      "$(ARTIFACTS)/src/ninja-$$8.tar.gz"


.PHONY: all
all: sources dist build test
