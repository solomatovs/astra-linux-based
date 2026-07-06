BASE_DIR      	?= .
ARTIFACTS     	?= $(BASE_DIR)/artifacts
BUILD_CONTEXT 	?= $(BASE_DIR)
TEST_DIR		?= $(BASE_DIR)/test


.DEFAULT_GOAL := help
.PHONY: help debug sources dist build test all

GLIBC_VERSION			?= 2.28
GCC_VERSION				?= 11.5.0
CMAKE_VERSION			?= 3.27.9
PYTHON_VERSION_SHORT	?= 3.11
MRUSTC_VERSION			?= 0.12.0
RUST_VERSION    		?= 1.90.0
NO_CACHE				?= 0

RUST_SRC_BASE			?= https://static.rust-lang.org/dist
MRUSTC_SRC_BASE			?= https://github.com/thepowersgang/mrustc/archive/refs/tags
APT_DEPS				?= patch curl git zlib1g-dev libssl-dev pkg-config

BUILD_IMAGE				:= dmp/gcc:$(GCC_VERSION)
RUNTIME_IMAGE			:= dmp/gcc:$(GCC_VERSION)
CMAKE_IMAGE				:= dmp/cmake:$(CMAKE_VERSION)-dist
PYTHON_IMAGE			:= dmp/python:$(PYTHON_VERSION_SHORT)-dist
IMAGE					:= dmp/rust:$(RUST_VERSION)
NO_CACHE_TEXT			:= $(if $(filter 1,$(NO_CACHE)),--no-cache,)

BUILD_ARGS	= \
	--build-arg BUILD_IMAGE=$(BUILD_IMAGE) 		\
	--build-arg RUNTIME_IMAGE=$(RUNTIME_IMAGE) 	\
	--build-arg CMAKE_IMAGE=$(CMAKE_IMAGE) 		\
	--build-arg PYTHON_IMAGE=$(PYTHON_IMAGE) 	\
	--build-arg MRUSTC_VERSION=$(MRUSTC_VERSION)\
	--build-arg APT_DEPS="$(APT_DEPS)"			\
	--build-arg RUST_VERSION=$(RUST_VERSION)


help:
	@echo "make:"
	@echo "  make all        собрать всё"
	@echo "  make sources    скачать mrustc-$(MRUSTC_VERSION) и rustc-$(RUST_VERSION)-src"
	@echo "  make dist       собрать промежуточный образ-дистрибутив $(IMAGE)-dist"
	@echo "  make build      собрать образ $(IMAGE)"
	@echo "  make test       проверить образ $(IMAGE): rustc, cargo, компиляция и запуск"
	@echo ""
	@echo "  rustc собирается ИЗ ИСХОДНИКОВ через mrustc (C++), без бинарного rustc."
	@echo "  Аналог golang: mrustc — C++-затравка, как Go 1.4 из C."
	@echo "  Сборщик: $(BUILD_IMAGE), cmake: $(CMAKE_IMAGE), python: $(PYTHON_IMAGE)."
	@echo ""


debug:
	@echo BUILD_IMAGE: $(BUILD_IMAGE)
	@echo RUNTIME_IMAGE: $(RUNTIME_IMAGE)
	@echo CMAKE_IMAGE: $(CMAKE_IMAGE)
	@echo PYTHON_IMAGE: $(PYTHON_IMAGE)
	@echo IMAGE: $(IMAGE)
	@echo MRUSTC_VERSION: $(MRUSTC_VERSION)
	@echo RUST_VERSION: $(RUST_VERSION)
	@echo NO_CACHE_TEXT: $(NO_CACHE_TEXT)


sources:
	@mkdir -p $(ARTIFACTS)/src
	@set -e; for spec in \
	  "$(ARTIFACTS)/src/mrustc-$(MRUSTC_VERSION).tar.gz=$(MRUSTC_SRC_BASE)/v$(MRUSTC_VERSION).tar.gz" \
	  "$(ARTIFACTS)/src/rustc-$(RUST_VERSION)-src.tar.gz=$(RUST_SRC_BASE)/rustc-$(RUST_VERSION)-src.tar.gz" \
	; do \
	  dest="$${spec%%=*}"; url="$${spec#*=}"; \
	  if [ -f "$$dest" ]; then echo "  ✓ $$dest"; \
	  else echo ">>> $$url"; curl -fSL -o "$$dest" "$$url"; fi; \
	done


dist:
	docker build -f Dockerfile --target dist $(NO_CACHE_TEXT) \
		$(BUILD_ARGS) \
		--tag $(IMAGE)-dist \
		--progress plain $(BUILD_CONTEXT)


build:
	docker build -f Dockerfile $(NO_CACHE_TEXT) \
		$(BUILD_ARGS) \
		--tag $(IMAGE) \
		--progress plain $(BUILD_CONTEXT)
	@docker images $(IMAGE)


test:
	docker run --rm --entrypoint /bin/bash -v $(TEST_DIR):/test:ro $(IMAGE) -euc '\
		echo "== versions =="; \
		rustc --version; \
		rustc --print sysroot; \
		cargo --version; \
		echo "== compile hello.rs (rustc) =="; \
		rustc -O /test/hello.rs -o /tmp/hello && /tmp/hello; \
		echo "== cargo new + run =="; \
		cp -r /test/demo /tmp/demo && cd /tmp/demo && cargo run --release --offline 2>&1 | tail -4'


all: sources dist build test
