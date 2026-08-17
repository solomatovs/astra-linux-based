BASE_DIR      	?= $(abspath .)
ARTIFACTS     	?= $(abspath $(BASE_DIR)/../../ci-artifacts/gcc)
BUILD_CONTEXT 	?= $(BASE_DIR)
# рабочая копия $(ARTIFACTS) внутри контекста сборки (хардлинки, см. artifacts-link)
LINKED_ARTIFACTS ?= $(BUILD_CONTEXT)/artifacts
TEST_DIR		?= $(BASE_DIR)/test

# путь, по которому дистрибутив лежит внутри -dist образа (GCC_PATH в Dockerfile)
GCC_PATH		?= /opt/gcc


.DEFAULT_GOAL := help

# список версий, которые поддерживает сборщик
# версии собираются одна за другой, строго по цепочке
VERSION_CHAIN		?= 8.5.0 9.5.0 10.5.0 11.5.0 12.4.0 13.3.0 14.2.0
# целевая версия сборки. по умолчанию берется последняя версия
# соответственно для ее сборки потребуется собрать все предыдущие версии в цепочки
VERSION      		?= $(lastword $(VERSION_CHAIN))
# базовый url адрес для скачивания исходников собираемых в Dockerfile
BASE_URL			?= https://ftp.gnu.org/gnu
# выполнять ли сброс кэша: docker build --no-cache
NO_CACHE			?= 0

GLIBC_IMAGE			?= dmp/glibc:2.28
GCC_IMAGE			?= dmp/gcc:8.5.0
PYTHON_IMAGE		?= dmp/python:3.10-dist

# зависимости для сборки разных версии
# версия           runtime_image	build_image		gmp     mpfr    mpc     binutils  make
DEPS_8.5.0		:= $(GLIBC_IMAGE)	$(GLIBC_IMAGE)	6.1.2   4.0.2   1.1.0   2.31.1    4.4.1
DEPS_9.5.0		:= $(GLIBC_IMAGE)	$(GCC_IMAGE)	6.1.2   4.0.2   1.1.0   2.31.1    4.4.1
DEPS_10.5.0		:= $(GLIBC_IMAGE)	$(GCC_IMAGE)	6.1.2   4.0.2   1.1.0   2.31.1    4.4.1
DEPS_11.5.0		:= $(GLIBC_IMAGE)	$(GCC_IMAGE)	6.1.2   4.0.2   1.1.0   2.31.1    4.4.1
DEPS_12.4.0		:= $(GLIBC_IMAGE)	$(GCC_IMAGE)	6.1.2   4.0.2   1.1.0   2.31.1    4.4.1
DEPS_13.3.0		:= $(GLIBC_IMAGE)	$(GCC_IMAGE)	6.1.2   4.0.2   1.1.0   2.31.1    4.4.1
DEPS_14.2.0		:= $(GLIBC_IMAGE)	$(GCC_IMAGE)	6.1.2   4.0.2   1.1.0   2.31.1    4.4.1

# базовое название для образа
IMAGE			:= dmp/gcc
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

.PHONY: sources-all dist-all extract-all build-all test-all clean-all
sources-all:		$(addprefix sources-,$(VERSION_CHAIN))
dist-all:		$(addprefix dist-,$(VERSION_CHAIN))
extract-all:		$(addprefix extract-,$(VERSION_CHAIN))
build-all:		$(addprefix build-,$(VERSION_CHAIN))
test-all:		$(addprefix test-,$(VERSION_CHAIN))
clean-all:		$(addprefix clean-,$(VERSION_CHAIN))


.PHONY: help
help:
	@echo "make:"
	@echo "  make all        собрать всё"
	@echo "  make sources    скачать исходники для $(VERSION)"
	@echo "  make dist       собрать -dist образ $(VERSION) (звенья цепочки — если нужны)"
	@echo "  make extract    достать дистрибутив ($(GCC_PATH)) из -dist образа в $(ARTIFACTS)/build/<версия>"
	@echo "  make build      собрать образ $(IMAGE)"
	@echo "  make test       проверить тулчейн: собрать и запустить test/hello.cpp в образе $(IMAGE)"
	@echo ""
	@echo "  Цепочка: $(VERSION_CHAIN)"
	@echo "  Первое звено ($(FIRST_VERSION)) — bootstrap через build-essential,"
	@echo "  каждое следующее собирается предыдущим gcc."
	@echo "  Зависимости (gmp/mpfr/mpc/binutils/make) задаются пословно в DEPS_<версия>."
	@echo "  Другая версия: make all VERSION=12.4.0"
	@echo ""
	@echo "  make <стадия>-all  то же для всех версий из VERSION_CHAIN"
	@echo "  FORCE=1            пересобрать, даже если образ уже есть"


.PHONY: debug
debug:
	$(info BASE_DIR       = $(BASE_DIR))
	$(info ARTIFACTS      = $(ARTIFACTS))
	$(info GCC_PATH       = $(GCC_PATH))
	$(info BUILD_CONTEXT  = $(BUILD_CONTEXT))
	$(info LINKED_ARTIFACTS = $(LINKED_ARTIFACTS))
	$(info TEST_DIR       = $(TEST_DIR))
	$(info VERSION_CHAIN  = $(VERSION_CHAIN))
	$(info VERSION        = $(VERSION))
	$(info FIRST_VERSION  = $(FIRST_VERSION))
	$(info BASE_URL       = $(BASE_URL))
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
	for spec in $$v										\
		"$(BASE_URL)/gmp/gmp-$$3.tar.xz" 				\
		"$(BASE_URL)/mpfr/mpfr-$$4.tar.xz" 				\
		"$(BASE_URL)/mpc/mpc-$$5.tar.gz" 				\
		"$(BASE_URL)/binutils/binutils-$$6.tar.xz" 		\
		"$(BASE_URL)/make/make-$$7.tar.gz" 				\
		"$(BASE_URL)/gcc/gcc-$$current/gcc-$$current.tar.xz"	\
	; do [ -f "$(ARTIFACTS)/src/$${spec##*/}" ] || curl -v -o "$(ARTIFACTS)/src/$${spec##*/}" "$$spec" ;
	done


# docker build --target dist - дистрибутив с скомпилированными исходниками (без установки в систему)
.PHONY: dist
dist: dist-$(VERSION)

.ONESHELL:
dist-%: $$(call need_dist,$$*)
	@current=$(*)
	set -e
	set -- $(DEPS_$*)
	[ -z "$(call built,$(IMAGE):$*-dist)" ] || { echo "  = $(IMAGE):$*-dist уже собран (FORCE=1 — пересобрать)"; exit 0; }
	docker build -f Dockerfile $(NO_CACHE_TEXT) --target dist	\
		--build-arg RUNTIME_IMAGE=$$1		\
		--build-arg BUILD_IMAGE=$$2 		\
		--build-arg GCC_VERSION=$$current 	\
		--build-arg GMP_VERSION=$$3 		\
		--build-arg MPFR_VERSION=$$4 		\
		--build-arg MPC_VERSION=$$5 		\
		--build-arg BINUTILS_VERSION=$$6 	\
		--build-arg MAKE_VERSION=$$7 		\
		--tag $(IMAGE):$$current-dist 		\
		--progress plain $(BUILD_CONTEXT)


# извлечь скомпилированный дистрибутив (/opt/gcc) из -dist образа в artifacts/build/<версия>
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
	docker cp "$$cid:$(GCC_PATH)/." "$$dst/"
	echo "extracted $$img:$(GCC_PATH) -> $$dst"
	du -h --max-depth=1 "$$dst" || true


# docker build - истоговый образ с установленным ПО (установка из дистрибутива)
.PHONY: build
build: build-$(VERSION)

.ONESHELL:
build-%: $$(call need_build,$$*)
	@current=$(*)
	set -e
	set -- $(DEPS_$*)
	[ -z "$(call built,$(IMAGE):$*)" ] || { echo "  = $(IMAGE):$* уже собран (FORCE=1 — пересобрать)"; exit 0; }
	docker build -f Dockerfile $(NO_CACHE_TEXT) \
		--build-arg DIST_IMAGE=$(IMAGE):$*-dist	\
		--build-arg RUNTIME_IMAGE=$$1			\
		--build-arg BUILD_IMAGE=$$2				\
		--build-arg GCC_VERSION=$$current		\
		--build-arg GMP_VERSION=$$3				\
		--build-arg MPFR_VERSION=$$4			\
		--build-arg MPC_VERSION=$$5				\
		--build-arg BINUTILS_VERSION=$$6		\
		--build-arg MAKE_VERSION=$$7			\
		--tag $(IMAGE):$$current				\
		--progress plain $(BUILD_CONTEXT)


# тестирование собранного образа
.PHONY: test
test: test-$(VERSION)

.ONESHELL:
test-%: build-%
	@current=$(*)
	docker run --rm --entrypoint /bin/bash -v $(TEST_DIR):/test:ro $(IMAGE):$$current -euc '\
		echo "== versions =="; 		\
		gcc  --version | head -1; 	\
		g++  --version | head -1; 	\
		as   --version | head -1; 	\
		ld   --version | head -1; 	\
		make --version | head -1; 	\
		echo "== compile =="; 		\
		g++ -O2 -Wall /test/hello.cpp -o /tmp/hello; \
		echo "== run =="; 			\
		/tmp/hello'


.PHONY: clean
clean: clean-$(VERSION)
	rm -rf "$(LINKED_ARTIFACTS)"

.ONESHELL:
clean-%:
	@current=$(*)
	set -e
	set -- $(DEPS_$*)
	mkdir -p $(ARTIFACTS)/src
	for spec in $$v								\
		"$(ARTIFACTS)/src/gmp-$$3.tar.xz" 		\
		"$(ARTIFACTS)/src/mpfr-$$4.tar.xz" 		\
		"$(ARTIFACTS)/src/mpc-$$5.tar.gz" 		\
		"$(ARTIFACTS)/src/binutils-$$6.tar.xz" 	\
		"$(ARTIFACTS)/src/make-$$7.tar.gz" 		\
		"$(ARTIFACTS)/src/gcc-$$current.tar.xz"	\
	; do rm -f "$$spec" ;
	done


.PHONY: all
all: sources dist build test
