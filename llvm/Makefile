BASE_DIR      	?= .
ARTIFACTS     	?= $(BASE_DIR)/artifacts
BUILD_CONTEXT 	?= $(BASE_DIR)
TEST_DIR		?= $(BASE_DIR)/test


.DEFAULT_GOAL := help
.PHONY: help sources dist build test all

GLIBC_VERSION			?= 2.28
GCC_VERSION				?= 11.5.0
CMAKE_VERSION			?= 3.27.9
PYTHON_VERSION_SHORT	?= 3.11
LLVM_VERSION			?= 20.1.8
NINJA_VERSION			?= 1.11.1
LLVM_TARGETS			?= X86
NO_CACHE				?= 0

LLVM_PROJECTS			?= clang;clang-tools-extra;lld;bolt;polly;mlir;flang;lldb
LLVM_RUNTIMES			?= compiler-rt;libcxx;libcxxabi;libunwind;openmp

BASE_IMAGE		:= dmp/gcc:$(GCC_VERSION)
RUNTIME_IMAGE	:= dmp/glibc:$(GLIBC_VERSION)
CMAKE_IMAGE		:= dmp/cmake:$(CMAKE_VERSION)-dist
PYTHON_IMAGE	:= dmp/python:$(PYTHON_VERSION_SHORT)-dist
IMAGE			:= dmp/llvm:$(LLVM_VERSION)
NO_CACHE_TEXT	:= $(if $(filter 1,$(NO_CACHE)),--no-cache,)


help:
	@echo "make:"
	@echo "  make all        собрать всё"
	@echo "  make sources    скачать исходники llvm-project-$(LLVM_VERSION) и ninja-$(NINJA_VERSION)"
	@echo "  make dist       собрать промежуточный образ-дистрибутив $(IMAGE)-dist"
	@echo "  make build      собрать образ $(IMAGE) (сборщик: $(BASE_IMAGE), cmake: $(CMAKE_IMAGE), python: $(PYTHON_IMAGE))"
	@echo "  make test       проверить образ $(IMAGE): llvm-config, llc, opt, таргеты"
	@echo ""


debug:
	@echo BASE_IMAGE: $(BASE_IMAGE)
	@echo CMAKE_IMAGE: $(CMAKE_IMAGE)
	@echo PYTHON_IMAGE: $(PYTHON_IMAGE)
	@echo IMAGE: $(IMAGE)


sources:
	@mkdir -p $(ARTIFACTS)/src
	@set -e; for spec in \
	  "$(ARTIFACTS)/src/llvm-project-$(LLVM_VERSION).src.tar.xz=https://github.com/llvm/llvm-project/releases/download/llvmorg-$(LLVM_VERSION)/llvm-project-$(LLVM_VERSION).src.tar.xz" \
	  "$(ARTIFACTS)/src/ninja-$(NINJA_VERSION).tar.gz=https://github.com/ninja-build/ninja/archive/refs/tags/v$(NINJA_VERSION).tar.gz" \
	; do \
	  dest="$${spec%%=*}"; url="$${spec#*=}"; \
	  if [ -f "$$dest" ]; then echo "  ✓ $$dest"; \
	  else echo ">>> $$url"; curl -fSL -o "$$dest" "$$url"; fi; \
	done


dist:
	docker build -f Dockerfile --target dist $(NO_CACHE_TEXT) \
		--build-arg BASE_IMAGE=$(BASE_IMAGE) \
		--build-arg RUNTIME_IMAGE=$(RUNTIME_IMAGE) \
		--build-arg CMAKE_IMAGE=$(CMAKE_IMAGE) \
		--build-arg PYTHON_IMAGE=$(PYTHON_IMAGE) \
		--build-arg LLVM_VERSION=$(LLVM_VERSION) \
		--build-arg NINJA_VERSION=$(NINJA_VERSION) \
		--build-arg LLVM_TARGETS=$(LLVM_TARGETS) \
		--build-arg LLVM_PROJECTS="$(LLVM_PROJECTS)" \
		--build-arg LLVM_RUNTIMES="$(LLVM_RUNTIMES)" \
		--tag $(IMAGE)-dist \
		--progress plain $(BUILD_CONTEXT)


build:
	docker build -f Dockerfile $(NO_CACHE_TEXT) \
		--build-arg BASE_IMAGE=$(BASE_IMAGE) \
		--build-arg RUNTIME_IMAGE=$(RUNTIME_IMAGE) \
		--build-arg CMAKE_IMAGE=$(CMAKE_IMAGE) \
		--build-arg PYTHON_IMAGE=$(PYTHON_IMAGE) \
		--build-arg LLVM_VERSION=$(LLVM_VERSION) \
		--build-arg NINJA_VERSION=$(NINJA_VERSION) \
		--build-arg LLVM_TARGETS=$(LLVM_TARGETS) \
		--build-arg LLVM_PROJECTS="$(LLVM_PROJECTS)" \
		--build-arg LLVM_RUNTIMES="$(LLVM_RUNTIMES)" \
		--tag $(IMAGE) \
		--progress plain $(BUILD_CONTEXT)


test:
	docker run --rm --entrypoint /bin/bash $(IMAGE) -euc '\
		echo "== llvm-config =="; \
		llvm-config --version; \
		llvm-config --host-target; \
		llvm-config --targets-built; \
		echo "== версии инструментов =="; \
		clang   --version | head -1; \
		clang++ --version | head -1; \
		ld.lld  --version; \
		llc     --version | head -4 | tail -1; \
		echo "== наличие компонентов =="; \
		for t in clang clang++ clang-tidy ld.lld lldb llvm-bolt mlir-opt opt llvm-ar; do \
			command -v $$t >/dev/null && echo "  ok   $$t" || echo "  MISS $$t"; \
		done'


all: sources dist build test
