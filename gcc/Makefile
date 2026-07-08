BASE_DIR      	?= $(abspath .)
ARTIFACTS     	?= $(BASE_DIR)/artifacts
BUILD_CONTEXT 	?= $(BASE_DIR)
TEST_DIR		?= $(BASE_DIR)/test


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


# helper: поиск предыдущей версии в VERSION_CHAIN
prev_of = $(shell echo "$(VERSION_CHAIN)" | awk -v v='$(1)' '{for(i=1;i<=NF;i++){if($$i==v){print p;exit};p=$$i}}')


.PHONY: help
help:
	@echo "make:"
	@echo "  make all        собрать всё"
	@echo "  make sources    скачать исходники для цепочки вплоть до $(VERSION)"
	@echo "  make dist       собрать цепочку -dist образов одну за другой до $(VERSION)"
	@echo "  make build      собрать образ $(IMAGE)"
	@echo "  make test       проверить тулчейн: собрать и запустить test/hello.cpp в образе $(IMAGE)"
	@echo ""
	@echo "  Цепочка: $(VERSION_CHAIN)"
	@echo "  Первое звено ($(FIRST_VERSION)) — bootstrap через build-essential,"
	@echo "  каждое следующее собирается предыдущим gcc."
	@echo "  Зависимости (gmp/mpfr/mpc/binutils/make) задаются пословно в DEPS_<версия>."
	@echo "  Другая версия: make all VERSION=12.4.0"
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
	for spec in $$v										\
		"$(BASE_URL)/gmp/gmp-$$3.tar.xz" 				\
		"$(BASE_URL)/mpfr/mpfr-$$4.tar.xz" 				\
		"$(BASE_URL)/mpc/mpc-$$5.tar.gz" 				\
		"$(BASE_URL)/binutils/binutils-$$6.tar.xz" 		\
		"$(BASE_URL)/make/make-$$7.tar.gz" 				\
		"$(BASE_URL)/gcc/gcc-$$current/gcc-$$current.tar.xz"	\
	; do curl -v --skip-existing -O --output-dir "$(ARTIFACTS)/src" $$spec ;
	done


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
	docker build -f Dockerfile $(NO_CACHE_TEXT) \
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

.ONESHELL:
clean-%:
	@current=$(*)
	@prev='$(call prev_of,$*)'
	# вызываем рекурсивно предыдущую версию
	[ -n "$$prev" ] && $(MAKE) clean-$$prev || true
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
