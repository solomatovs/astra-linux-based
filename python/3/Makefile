BASE_DIR      	?= .
ARTIFACTS     	?= $(BASE_DIR)/artifacts
BUILD_CONTEXT 	?= $(BASE_DIR)
TEST_DIR		?= $(BASE_DIR)/test


.DEFAULT_GOAL := help
.PHONY: help debug sources dist build test all

GLIBC_VERSION		?= 2.28
GCC_VERSION      	?= 8.5.0
NO_CACHE			?= 0

PYTHON_VERSIONS		?= 3.8.20 3.9.25 3.10.20 3.11.15 3.12.13 3.13.14 3.14.6
PYTHON_SRC_BASE		?= https://www.python.org/ftp/python

# ── apt-зависимости по ПОЛНОЙ версии python ──
DEPS_3.8.20		:= zlib1g-dev libncurses5-dev libgdbm-dev libnss3-dev libssl-dev libreadline-dev libffi-dev libsqlite3-dev libbz2-dev liblzma-dev curl
DEPS_3.9.25		:= zlib1g-dev libncurses5-dev libgdbm-dev libnss3-dev libssl-dev libreadline-dev libffi-dev libsqlite3-dev libbz2-dev liblzma-dev curl
DEPS_3.10.20	:= zlib1g-dev libncurses5-dev libgdbm-dev libnss3-dev libssl-dev libreadline-dev libffi-dev libsqlite3-dev libbz2-dev liblzma-dev curl
DEPS_3.11.15	:= zlib1g-dev libncurses5-dev libgdbm-dev libnss3-dev libssl-dev libreadline-dev libffi-dev libsqlite3-dev libbz2-dev liblzma-dev curl
DEPS_3.12.13	:= zlib1g-dev libncurses5-dev libgdbm-dev libnss3-dev libssl-dev libreadline-dev libffi-dev libsqlite3-dev libbz2-dev liblzma-dev curl
DEPS_3.13.14	:= zlib1g-dev libncurses5-dev libgdbm-dev libnss3-dev libssl-dev libreadline-dev libffi-dev libsqlite3-dev libbz2-dev liblzma-dev curl
DEPS_3.14.6		:= zlib1g-dev libncurses5-dev libgdbm-dev libnss3-dev libssl-dev libreadline-dev libffi-dev libsqlite3-dev libbz2-dev liblzma-dev curl

# case-ветки apt-зависимостей для shell-цикла (по образцу gcc/Makefile)
deps_arm	= $(1)) echo "$(DEPS_$(1))" ;;
DEPS_CASE	:= $(foreach V,$(PYTHON_VERSIONS),$(call deps_arm,$(V)))

BASE_IMAGE			:= dmp/glibc:$(GLIBC_VERSION)
BUILD_IMAGE			:= dmp/gcc:$(GCC_VERSION)
NO_CACHE_TEXT		:= $(if $(filter 1,$(NO_CACHE)),--no-cache,)


help:
	@echo "make:"
	@echo "  make all        собрать всё для версий: $(PYTHON_VERSIONS)"
	@echo "  make sources    скачать исходники Python для этих версий"
	@echo "  make dist       собрать промежуточные образы dmp/python:<версия>-dist"
	@echo "  make build      собрать образы dmp/python:<версия>"
	@echo "  make test       проверить образы: python, pip, C-модули, ssl, sqlite, ffi"
	@echo ""
	@echo "  Версии (полные): $(PYTHON_VERSIONS)"
	@echo "  Сборщик: $(BUILD_IMAGE), рантайм: $(BASE_IMAGE)"
	@echo "  Каждая версия независима; apt-зависимости — в DEPS_<полная-версия>."
	@echo "  Каждый образ тегируется полной и короткой версией (напр. 3.12.13 и 3.12)."
	@echo "  Одна версия: make all PYTHON_VERSIONS=3.11.15"
	@echo ""


debug:
	@echo BASE_IMAGE: $(BASE_IMAGE)
	@echo BUILD_IMAGE: $(BUILD_IMAGE)
	@echo PYTHON_VERSIONS: $(PYTHON_VERSIONS)
	@echo NO_CACHE_TEXT: $(NO_CACHE_TEXT)
	@deps() { case "$$1" in $(DEPS_CASE) *) echo "(нет DEPS_$$1)" ;; esac; }; \
	for v in $(PYTHON_VERSIONS); do \
	  echo "  $$v  short=$${v%.*}  image=dmp/python:$$v  apt=[$$(deps "$$v")]"; \
	done


sources:
	@mkdir -p $(ARTIFACTS)/src
	@set -e; \
	for v in $(PYTHON_VERSIONS); do \
	  dest="$(ARTIFACTS)/src/Python-$$v.tar.xz"; \
	  if [ -f "$$dest" ]; then echo "  ✓ $$dest"; \
	  else url="$(PYTHON_SRC_BASE)/$$v/Python-$$v.tar.xz"; \
	    echo ">>> $$url"; curl -fSL -o "$$dest" "$$url"; fi; \
	done


dist: sources
	@deps() { case "$$1" in $(DEPS_CASE) *) echo "нет зависимостей для python $$1 (добавь DEPS_$$1)" >&2; exit 1 ;; esac; }; \
	set -e; \
	for v in $(PYTHON_VERSIONS); do \
	  short="$${v%.*}"; apt="$$(deps "$$v")"; \
	  [ -n "$$apt" ] || { echo "нет apt-зависимостей для python $$v (заполни DEPS_$$v)" >&2; exit 1; }; \
	  echo ">>> python $$v (dist)"; \
	  docker build -f Dockerfile --target dist $(NO_CACHE_TEXT) \
	    --build-arg BASE_IMAGE=$(BASE_IMAGE) \
	    --build-arg BUILD_IMAGE=$(BUILD_IMAGE) \
	    --build-arg PYTHON_VERSION=$$v \
	    --build-arg PYTHON_VERSION_SHORT=$$short \
	    --build-arg APT_DEPS="$$apt" \
	    --tag dmp/python:$$v-dist \
	    --tag dmp/python:$$short-dist \
	    --progress plain $(BUILD_CONTEXT); \
	done


build: dist
	@deps() { case "$$1" in $(DEPS_CASE) *) echo "нет зависимостей для python $$1 (добавь DEPS_$$1)" >&2; exit 1 ;; esac; }; \
	set -e; \
	for v in $(PYTHON_VERSIONS); do \
	  short="$${v%.*}"; apt="$$(deps "$$v")"; \
	  [ -n "$$apt" ] || { echo "нет apt-зависимостей для python $$v (заполни DEPS_$$v)" >&2; exit 1; }; \
	  echo ">>> python $$v (build)"; \
	  docker build -f Dockerfile $(NO_CACHE_TEXT) \
	    --build-arg BASE_IMAGE=$(BASE_IMAGE) \
	    --build-arg BUILD_IMAGE=$(BUILD_IMAGE) \
	    --build-arg PYTHON_VERSION=$$v \
	    --build-arg PYTHON_VERSION_SHORT=$$short \
	    --build-arg APT_DEPS="$$apt" \
	    --tag dmp/python:$$v \
	    --tag dmp/python:$$short \
	    --progress plain $(BUILD_CONTEXT); \
	done


test:
	@set -e; \
	for v in $(PYTHON_VERSIONS); do \
	  echo ">>> test dmp/python:$$v"; \
	  docker run --rm --entrypoint /bin/bash -v $(TEST_DIR):/test:ro dmp/python:$$v -euc '\
	    echo "== interpreter & pip =="; \
	    python3 --version; \
	    python3 -c "import sys; print(sys.executable)"; \
	    python3 -m pip --version; \
	    python3 -m pip list 2>/dev/null | head -n 20; \
	    echo "== functional checks =="; \
	    python3 /test/pycheck.py'; \
	done


all: sources dist build test
