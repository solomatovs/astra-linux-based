BASE_DIR      	?= .
ARTIFACTS     	?= $(BASE_DIR)/artifacts
BUILD_CONTEXT 	?= $(BASE_DIR)
TEST_DIR		?= $(BASE_DIR)/test


.DEFAULT_GOAL := help
.PHONY: help sources dist build test all

GLIBC_VERSION	?= 2.28
GCC_VERSION		?= 11.5.0
CMAKE_VERSION	?= 3.27.9
NO_CACHE		?= 0

BASE_IMAGE		:= dmp/gcc:$(GCC_VERSION)
RUNTIME_IMAGE	:= dmp/glibc:$(GLIBC_VERSION)
IMAGE			:= dmp/cmake:$(CMAKE_VERSION)
NO_CACHE_TEXT	:= $(if $(filter 1,$(NO_CACHE)),--no-cache,)


help:
	@echo "make:"
	@echo "  make all        собрать всё"
	@echo "  make sources    скачать исходники cmake-$(CMAKE_VERSION)"
	@echo "  make dist       собрать промежуточный образ-дистрибутив $(IMAGE)-dist"
	@echo "  make build      собрать образ $(IMAGE) (сборщик: $(BASE_IMAGE))"
	@echo "  make test       проверить образ $(IMAGE): версия, capabilities, configure NONE-проекта"
	@echo ""


debug:
	@echo BASE_IMAGE: $(BASE_IMAGE)
	@echo RUNTIME_IMAGE: $(RUNTIME_IMAGE)
	@echo IMAGE: $(IMAGE)


sources:
	@mkdir -p $(ARTIFACTS)/src
	@dest="$(ARTIFACTS)/src/cmake-$(CMAKE_VERSION).tar.gz"; \
	if [ -f "$$dest" ]; then echo "  ✓ $$dest"; \
	else url="https://github.com/Kitware/CMake/releases/download/v$(CMAKE_VERSION)/cmake-$(CMAKE_VERSION).tar.gz"; \
	  echo ">>> $$url"; curl -fSL -o "$$dest" "$$url"; fi


dist:
	docker build -f Dockerfile --target dist $(NO_CACHE_TEXT) \
		--build-arg BASE_IMAGE=$(BASE_IMAGE) \
		--build-arg RUNTIME_IMAGE=$(RUNTIME_IMAGE) \
		--build-arg CMAKE_VERSION=$(CMAKE_VERSION) \
		--tag $(IMAGE)-dist \
		--progress plain $(BUILD_CONTEXT)


build:
	docker build -f Dockerfile $(NO_CACHE_TEXT) \
		--build-arg BASE_IMAGE=$(BASE_IMAGE) \
		--build-arg RUNTIME_IMAGE=$(RUNTIME_IMAGE) \
		--build-arg CMAKE_VERSION=$(CMAKE_VERSION) \
		--tag $(IMAGE) \
		--progress plain $(BUILD_CONTEXT)


test:
	docker run --rm --entrypoint /bin/bash -v $(TEST_DIR):/test:ro $(IMAGE) -euc '\
		echo "== version =="; \
		cmake --version; \
		echo "== capabilities (generators) =="; \
		cmake -E capabilities | head -c 200; echo; \
		echo "== script-режим (cmake -P) =="; \
		cmake -P /test/selfcheck.cmake; \
		echo "== cmake -E =="; \
		cmake -E make_directory /tmp/x && cmake -E echo "cmake -E works"'


all: sources dist build test
