BASE_DIR      	?= .
ARTIFACTS     	?= $(BASE_DIR)/artifacts
BUILD_CONTEXT 	?= $(BASE_DIR)
TEST_DIR		?= $(BASE_DIR)/test


.DEFAULT_GOAL := help
.PHONY: help debug sources image debs dist build test all

ASTRA_VERSION 			?= 2.12
PYTHON_VERSION_SHORT	?= 3.11
NO_CACHE				?= 0

GLIBC_CHAIN			?= 2.28 2.29 2.30 2.31 2.32 2.34 2.35 2.36 2.37 2.38 2.39 2.40 2.41 2.42 2.43
GLIBC_VERSION      	?= 2.28
GLIBC_SRC_BASE		?= https://ftp.gnu.org/gnu/glibc

# gcc-образ по умолчанию для компиляции glibc (перекрывается пословно в DEPS_<версия>)
GCC_DEFAULT			?= 14.2.0

# ── зависимости по версиям glibc: "python gcc-образ" ──
#                  python  gcc
DEPS_2.28		:=
DEPS_2.29		:= 3.11    14.2.0

# резолв компонента конкретной версии (make-уровень): $(call python_of,2.29) / $(call gcc_of,2.29)
python_of	= $(or $(word 1,$(DEPS_$(1))),$(PYTHON_VERSION_SHORT))
gcc_of		= $(or $(word 2,$(DEPS_$(1))),$(GCC_DEFAULT))

# те же словари для shell-циклов: pyof/gccof <glibc>
py_arm		= $(1)) echo "$(call python_of,$(1))" ;;
PY_CASE		:= $(foreach V,$(GLIBC_CHAIN),$(call py_arm,$(V)))
gcc_arm		= $(1)) echo "$(call gcc_of,$(1))" ;;
GCC_CASE	:= $(foreach V,$(GLIBC_CHAIN),$(call gcc_arm,$(V)))

OREL_IMAGE    		:= registry.astralinux.ru/library/orel:$(ASTRA_VERSION)
OREL_TAR      		:= $(ARTIFACTS)/orel-$(ASTRA_VERSION).tar
IMAGE         		:= dmp/glibc:$(GLIBC_VERSION)
APT_DEB_DIR   		:= $(ARTIFACTS)/apt-bootstrap
DEB_PACKAGES  		:= apt-transport-https
NO_CACHE_TEXT		:= $(if $(filter 1,$(NO_CACHE)),--no-cache,)
GLIBC_FIRST			:= $(firstword $(GLIBC_CHAIN))
GLIBC_BOOTSTRAP		= $(shell echo "$(GLIBC_CHAIN)" | awk -v v='$(GLIBC_VERSION)' '{for(i=1;i<=NF;i++){if($$i==v){print p;exit};p=$$i}}')

PYTHON_ARG		= --build-arg PYTHON_IMAGE=dmp/python:$(call python_of,$(GLIBC_VERSION))-dist
GCC_ARG			= --build-arg GCC_IMAGE=dmp/gcc:$(call gcc_of,$(GLIBC_VERSION))


help:
	@echo "make:"
	@echo "  make all        собрать всё"
	@echo "  make sources    скачать исходники glibc для цепочки вплоть до $(GLIBC_VERSION)"
	@echo "  make image      скачать образ astra $(OREL_IMAGE) -> $(OREL_TAR)"
	@echo "  make debs       скачать .deb ($(DEB_PACKAGES)) через orel -> $(APT_DEB_DIR)/"
	@echo "  make dist       собрать цепочку одну за другой до $(GLIBC_VERSION)"
	@echo "  make build      собрать образ $(IMAGE)"
	@echo "  make test       проверить образ $(IMAGE): версия glibc, заголовки, crt, защита от 2.24"
	@echo ""
	@echo "  Цепочка: $(GLIBC_CHAIN)"
	@echo "  Первое звено ($(GLIBC_FIRST)) — Dockerfile.bootstrap, из orel (glibc 2.24), apt-тулчейн."
	@echo "  Каждое следующее — общий Dockerfile: база = предыдущий glibc, компилятор = gcc-образ,"
	@echo "  python из python-dist (аптовый build-essential не собирает AVX-512 EVEX glibc 2.37+)."
	@echo "  Python и gcc-образ задаются пословно в DEPS_<версия> (по умолчанию gcc $(GCC_DEFAULT))."
	@echo "  Другая версия: make all GLIBC_VERSION=2.28"
	@echo ""


debug:
	@echo OREL_IMAGE: $(OREL_IMAGE)
	@echo IMAGE: $(IMAGE)
	@echo GLIBC_CHAIN: $(GLIBC_CHAIN)
	@echo GLIBC_VERSION: $(GLIBC_VERSION)
	@echo GLIBC_FIRST: $(GLIBC_FIRST)
	@echo GLIBC_BOOTSTRAP: $(GLIBC_BOOTSTRAP)
	@echo 'DEPS($(GLIBC_VERSION)): python=$(call python_of,$(GLIBC_VERSION)) gcc=$(call gcc_of,$(GLIBC_VERSION))'
	@echo NO_CACHE_TEXT: $(NO_CACHE_TEXT)


sources:
	@mkdir -p $(ARTIFACTS)/glibc-src
	@set -e; \
	for v in $(GLIBC_CHAIN); do \
	  dest="$(ARTIFACTS)/glibc-src/glibc-$$v.tar.xz"; \
	  if [ -f "$$dest" ]; then echo "  ✓ $$dest"; \
	  else url="$(GLIBC_SRC_BASE)/glibc-$$v.tar.xz"; \
	    echo ">>> $$url"; curl -fSL -o "$$dest" "$$url"; fi; \
	  [ "$$v" = "$(GLIBC_VERSION)" ] && break; \
	done
	@if [ -f "$(OREL_TAR)" ]; then echo "  ✓ $(OREL_TAR)"; \
	else echo "  ! нет $(OREL_TAR) — положи tar-архив базового образа astra в $(ARTIFACTS)/ вручную (или make image)"; fi


image:
	@mkdir -p $(ARTIFACTS)
	@if [ -f "$(OREL_TAR)" ]; then echo "  ✓ $(OREL_TAR)"; \
	else echo ">>> docker pull $(OREL_IMAGE)"; docker pull $(OREL_IMAGE); \
	  echo ">>> docker save -> $(OREL_TAR)"; docker save -o "$(OREL_TAR)" $(OREL_IMAGE); \
	  echo "  ✓ $(OREL_TAR)"; fi


debs:
	@mkdir -p $(APT_DEB_DIR)
	@docker image inspect $(OREL_IMAGE) >/dev/null 2>&1 || \
	  { if [ -f "$(OREL_TAR)" ]; then echo ">>> docker load -i $(OREL_TAR)"; docker load -i "$(OREL_TAR)"; \
	    else echo "  ! образ $(OREL_IMAGE) не найден — сделай 'make image', затем 'docker load -i $(OREL_TAR)'"; exit 1; fi; }
	docker run --rm \
	  -v "$(abspath $(APT_DEB_DIR))":/out \
	  $(OREL_IMAGE) \
	  bash -euc 'apt-get update && \
	    apt-get install -y --reinstall --download-only $(DEB_PACKAGES) && \
	    cp -v /var/cache/apt/archives/*.deb /out/ && \
	    chmod -R a+rwX /out'
	@echo "  ✓ .deb -> $(APT_DEB_DIR)/"
	@ls -1 $(APT_DEB_DIR)/*.deb 2>/dev/null || echo "  ! ни одного .deb не скачано"


dist: sources
	@pyof()  { case "$$1" in $(PY_CASE)  *) echo "$(PYTHON_VERSION_SHORT)" ;; esac; }; \
	gccof() { case "$$1" in $(GCC_CASE) *) echo "$(GCC_DEFAULT)" ;; esac; }; \
	set -e; prev=""; \
	for v in $(GLIBC_CHAIN); do \
	  if [ -z "$$prev" ]; then \
	    echo ">>> glibc $$v (bootstrap: orel)"; \
	    docker build -f Dockerfile.bootstrap --target glibc $(NO_CACHE_TEXT) \
	      --build-arg BASE_IMAGE=$(OREL_IMAGE) \
	      --build-arg GLIBC_VERSION=$$v \
	      --tag dmp/glibc:$$v \
	      --progress plain $(BUILD_CONTEXT); \
	  else \
	    py=$$(pyof "$$v"); gcc=$$(gccof "$$v"); \
	    echo ">>> glibc $$v (base: dmp/glibc:$$prev, python: $$py, gcc: $$gcc)"; \
	    docker build -f Dockerfile --target glibc $(NO_CACHE_TEXT) \
	      --build-arg BASE_IMAGE=dmp/glibc:$$prev \
	      --build-arg PYTHON_IMAGE=dmp/python:$$py-dist \
	      --build-arg GCC_IMAGE=dmp/gcc:$$gcc \
	      --build-arg GLIBC_VERSION=$$v \
	      --tag dmp/glibc:$$v \
	      --progress plain $(BUILD_CONTEXT); \
	  fi; \
	  prev=$$v; \
	  [ "$$v" = "$(GLIBC_VERSION)" ] && break; \
	done


build: dist
	@set -e; \
	if [ "$(GLIBC_VERSION)" = "$(GLIBC_FIRST)" ]; then \
	  docker build -f Dockerfile.bootstrap --target glibc $(NO_CACHE_TEXT) \
	    --build-arg BASE_IMAGE=$(OREL_IMAGE) \
	    --build-arg GLIBC_VERSION=$(GLIBC_VERSION) \
	    --tag $(IMAGE) \
	    --progress plain $(BUILD_CONTEXT); \
	else \
	  docker build -f Dockerfile --target glibc $(NO_CACHE_TEXT) \
	    --build-arg BASE_IMAGE=dmp/glibc:$(GLIBC_BOOTSTRAP) \
	    $(PYTHON_ARG) \
	    $(GCC_ARG) \
	    --build-arg GLIBC_VERSION=$(GLIBC_VERSION) \
	    --tag $(IMAGE) \
	    --progress plain $(BUILD_CONTEXT); \
	fi
	@docker images $(IMAGE)


test:
	@docker run --rm --entrypoint /bin/bash -v $(TEST_DIR):/test:ro $(IMAGE) -euc '\
		echo "== 1. runtime glibc =="; \
		ldd --version | head -1; \
		getconf GNU_LIBC_VERSION; \
		echo "== 2. версия заголовков glibc =="; \
		grep "define\s*__GLIBC_MINOR__" /usr/include/features.h; \
		echo "== 3. kernel-заголовки (linux-libc-dev) =="; \
		ls /usr/include/linux/limits.h /usr/include/x86_64-linux-gnu/asm/unistd.h /usr/include/asm-generic/errno.h; \
		echo "== 4. crt-объекты + libc.so (линковка) =="; \
		ls /usr/lib/x86_64-linux-gnu/crt1.o /usr/lib/x86_64-linux-gnu/crti.o /usr/lib/x86_64-linux-gnu/crtn.o /usr/lib/x86_64-linux-gnu/libc.so; \
		echo "== 5. fake libc6-dev на hold =="; \
		dpkg -l libc6-dev | tail -1; \
		apt-mark showhold | grep -qx libc6-dev && echo "hold: OK" || { echo "hold: FAIL"; exit 1; }; \
		echo "== 6. apt install НЕ откатывает заголовки =="; \
		apt-get update >/dev/null 2>&1; \
		apt-get install -y --no-install-recommends build-essential zlib1g-dev >/dev/null 2>&1; \
		grep "define\s*__GLIBC_MINOR__" /usr/include/features.h; \
		echo "== 7. компиляция и запуск проверочной программы =="; \
		gcc -Wall -O2 /test/glibc_check.c -o /tmp/glibc_check; \
		/tmp/glibc_check'


all: sources image debs dist build test
