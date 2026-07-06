BASE_DIR      	?= .
ARTIFACTS     	?= $(BASE_DIR)/artifacts
BUILD_CONTEXT 	?= $(BASE_DIR)
TEST_DIR		?= $(BASE_DIR)/test


.DEFAULT_GOAL := help
.PHONY: help debug sources dist build test all

ASTRA_VERSION 		?= 2.12
GLIBC_VERSION		?= 2.28
NO_CACHE			?= 0

GCC_CHAIN			?= 8.5.0 9.5.0 10.5.0 11.5.0 12.4.0 13.3.0 14.2.0
GCC_VERSION      	?= 14.2.0
GCC_SRC_BASE		?= https://ftp.gnu.org/gnu/gcc

# зависимости по версиям gcc: "gmp mpfr mpc binutils make" ──
#                  gmp     mpfr    mpc     binutils  make
DEPS_8.5.0		:= 6.1.2   4.0.2   1.1.0   2.31.1    4.4.1
DEPS_9.5.0		:= 6.1.2   4.0.2   1.1.0   2.31.1    4.4.1
DEPS_10.5.0		:= 6.1.2   4.0.2   1.1.0   2.31.1    4.4.1
DEPS_11.5.0		:= 6.1.2   4.0.2   1.1.0   2.31.1    4.4.1
DEPS_12.4.0		:= 6.1.2   4.0.2   1.1.0   2.31.1    4.4.1
DEPS_13.3.0		:= 6.1.2   4.0.2   1.1.0   2.31.1    4.4.1
DEPS_14.2.0		:= 6.1.2   4.0.2   1.1.0   2.31.1    4.4.1

gmp_of		= $(word 1,$(DEPS_$(1)))
mpfr_of		= $(word 2,$(DEPS_$(1)))
mpc_of		= $(word 3,$(DEPS_$(1)))
binutils_of	= $(word 4,$(DEPS_$(1)))
make_of		= $(word 5,$(DEPS_$(1)))

deps_arm	= $(1)) echo "$(DEPS_$(1))" ;;
DEPS_CASE	:= $(foreach V,$(GCC_CHAIN),$(call deps_arm,$(V)))

BASE_IMAGE		 	:= dmp/glibc:$(GLIBC_VERSION)
IMAGE			 	:= dmp/gcc:$(GCC_VERSION)
NO_CACHE_TEXT		:= $(if $(filter 1,$(NO_CACHE)),--no-cache,)
GCC_FIRST			:= $(firstword $(GCC_CHAIN))
GCC_BOOTSTRAP		= $(shell echo "$(GCC_CHAIN)" | awk -v v='$(GCC_VERSION)' '{for(i=1;i<=NF;i++){if($$i==v){print p;exit};p=$$i}}')

PREREQ_ARGS		= \
	--build-arg GMP_VERSION=$(call gmp_of,$(GCC_VERSION)) \
	--build-arg MPFR_VERSION=$(call mpfr_of,$(GCC_VERSION)) \
	--build-arg MPC_VERSION=$(call mpc_of,$(GCC_VERSION)) \
	--build-arg BINUTILS_VERSION=$(call binutils_of,$(GCC_VERSION)) \
	--build-arg MAKE_VERSION=$(call make_of,$(GCC_VERSION))


help:
	@echo "make:"
	@echo "  make all        собрать всё"
	@echo "  make sources    скачать исходники для цепочки вплоть до $(GCC_VERSION)"
	@echo "  make dist       собрать цепочку -dist образов одну за другой до $(GCC_VERSION)"
	@echo "  make build      собрать образ $(IMAGE) (рантайм: $(BASE_IMAGE))"
	@echo "  make test       проверить тулчейн: собрать и запустить test/hello.cpp в образе $(IMAGE)"
	@echo ""
	@echo "  Цепочка: $(GCC_CHAIN)"
	@echo "  Первое звено ($(GCC_FIRST)) — bootstrap через build-essential,"
	@echo "  каждое следующее собирается предыдущим gcc."
	@echo "  Зависимости (gmp/mpfr/mpc/binutils/make) задаются пословно в DEPS_<версия>."
	@echo "  Другая версия: make all GCC_VERSION=12.4.0"
	@echo ""


debug:
	@echo BASE_IMAGE: $(BASE_IMAGE)
	@echo IMAGE: $(IMAGE)
	@echo GCC_CHAIN: $(GCC_CHAIN)
	@echo GCC_VERSION: $(GCC_VERSION)
	@echo GCC_FIRST: $(GCC_FIRST)
	@echo GCC_BOOTSTRAP: $(GCC_BOOTSTRAP)
	@echo 'DEPS($(GCC_VERSION)): gmp=$(call gmp_of,$(GCC_VERSION)) mpfr=$(call mpfr_of,$(GCC_VERSION)) mpc=$(call mpc_of,$(GCC_VERSION)) binutils=$(call binutils_of,$(GCC_VERSION)) make=$(call make_of,$(GCC_VERSION))'
	@echo NO_CACHE_TEXT: $(NO_CACHE_TEXT)


sources:
	@mkdir -p $(ARTIFACTS)/src
	@deps() { case "$$1" in $(DEPS_CASE) *) echo "нет зависимостей для gcc $$1 (добавь DEPS_$$1)" >&2; exit 1 ;; esac; }; \
	set -e; \
	for v in $(GCC_CHAIN); do \
	  set -- $$(deps "$$v"); gmp=$$1; mpfr=$$2; mpc=$$3; bu=$$4; mk=$$5; \
	  for spec in \
	    "$(ARTIFACTS)/src/gmp-$$gmp.tar.xz=https://ftp.gnu.org/gnu/gmp/gmp-$$gmp.tar.xz" \
	    "$(ARTIFACTS)/src/mpfr-$$mpfr.tar.xz=https://ftp.gnu.org/gnu/mpfr/mpfr-$$mpfr.tar.xz" \
	    "$(ARTIFACTS)/src/mpc-$$mpc.tar.gz=https://ftp.gnu.org/gnu/mpc/mpc-$$mpc.tar.gz" \
	    "$(ARTIFACTS)/src/binutils-$$bu.tar.xz=https://ftp.gnu.org/gnu/binutils/binutils-$$bu.tar.xz" \
	    "$(ARTIFACTS)/src/make-$$mk.tar.gz=https://ftp.gnu.org/gnu/make/make-$$mk.tar.gz" \
	    "$(ARTIFACTS)/src/gcc-$$v.tar.xz=$(GCC_SRC_BASE)/gcc-$$v/gcc-$$v.tar.xz" \
	  ; do \
	    dest="$${spec%%=*}"; url="$${spec#*=}"; \
	    if [ -f "$$dest" ]; then echo "  ✓ $$dest"; \
	    else echo ">>> $$url"; curl -fSL -o "$$dest" "$$url"; fi; \
	  done; \
	  [ "$$v" = "$(GCC_VERSION)" ] && break; \
	done


dist: sources
	@deps() { case "$$1" in $(DEPS_CASE) *) echo "нет зависимостей для gcc $$1 (добавь DEPS_$$1)" >&2; exit 1 ;; esac; }; \
	set -e; prev=""; \
	for v in $(GCC_CHAIN); do \
	  set -- $$(deps "$$v"); gmp=$$1; mpfr=$$2; mpc=$$3; bu=$$4; mk=$$5; \
	  if [ -z "$$prev" ]; then \
	    echo ">>> gcc $$v (bootstrap: build-essential)"; \
	    docker build -f Dockerfile --target dist $(NO_CACHE_TEXT) \
	      --build-arg BASE_IMAGE=$(BASE_IMAGE) \
	      --build-arg BOOTSTRAP=1 \
	      --build-arg GCC_VERSION=$$v \
	      --build-arg GMP_VERSION=$$gmp \
	      --build-arg MPFR_VERSION=$$mpfr \
	      --build-arg MPC_VERSION=$$mpc \
	      --build-arg BINUTILS_VERSION=$$bu \
	      --build-arg MAKE_VERSION=$$mk \
	      --tag dmp/gcc:$$v-dist \
	      --progress plain $(BUILD_CONTEXT); \
	  else \
	    echo ">>> gcc $$v (bootstrap: $$prev)"; \
	    docker build -f Dockerfile --target dist $(NO_CACHE_TEXT) \
	      --build-arg BASE_IMAGE=$(BASE_IMAGE) \
	      --build-arg BOOTSTRAP=0 \
	      --build-arg GCC_PREV_IMAGE=dmp/gcc:$$prev-dist \
	      --build-arg GCC_VERSION=$$v \
	      --build-arg GMP_VERSION=$$gmp \
	      --build-arg MPFR_VERSION=$$mpfr \
	      --build-arg MPC_VERSION=$$mpc \
	      --build-arg BINUTILS_VERSION=$$bu \
	      --build-arg MAKE_VERSION=$$mk \
	      --tag dmp/gcc:$$v-dist \
	      --progress plain $(BUILD_CONTEXT); \
	  fi; \
	  prev=$$v; \
	  [ "$$v" = "$(GCC_VERSION)" ] && break; \
	done


build: dist
	@set -e; \
	if [ "$(GCC_VERSION)" = "$(GCC_FIRST)" ]; then \
	  docker build -f Dockerfile $(NO_CACHE_TEXT) \
	    --build-arg BASE_IMAGE=$(BASE_IMAGE) \
	    --build-arg BOOTSTRAP=1 \
	    --build-arg GCC_VERSION=$(GCC_VERSION) \
	    $(PREREQ_ARGS) \
	    --tag $(IMAGE) \
	    --progress plain $(BUILD_CONTEXT); \
	else \
	  docker build -f Dockerfile $(NO_CACHE_TEXT) \
	    --build-arg BASE_IMAGE=$(BASE_IMAGE) \
	    --build-arg BOOTSTRAP=0 \
	    --build-arg GCC_PREV_IMAGE=dmp/gcc:$(GCC_BOOTSTRAP)-dist \
	    --build-arg GCC_VERSION=$(GCC_VERSION) \
	    $(PREREQ_ARGS) \
	    --tag $(IMAGE) \
	    --progress plain $(BUILD_CONTEXT); \
	fi


test:
	docker run --rm --entrypoint /bin/bash -v $(TEST_DIR):/test:ro $(IMAGE) -euc '\
		echo "== versions =="; \
		gcc  --version | head -1; \
		g++  --version | head -1; \
		as   --version | head -1; \
		ld   --version | head -1; \
		make --version | head -1; \
		echo "== compile =="; \
		g++ -O2 -Wall /test/hello.cpp -o /tmp/hello; \
		echo "== run =="; \
		/tmp/hello'


all: sources dist build test
