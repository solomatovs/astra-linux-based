BASE_DIR      	?= .
ARTIFACTS     	?= $(BASE_DIR)/artifacts
BUILD_CONTEXT 	?= $(BASE_DIR)
TEST_DIR		?= $(BASE_DIR)/test


.DEFAULT_GOAL := help
.PHONY: help sources dist build test all

GLIBC_VERSION			?= 2.28
GCC_VERSION				?= 11.5.0
LLVM_VERSION			?= 20.1.8
PYTHON_VERSION_SHORT	?= 3.11
NODE_VERSION			?= 22.11.0
NO_CACHE				?= 0

VERSION_WORDS		:= $(subst ., ,$(NODE_VERSION))
NODE_VERSION_MAJOR	:= $(word 1,$(VERSION_WORDS))

BASE_IMAGE		:= dmp/glibc:$(GLIBC_VERSION)
RUNTIME_IMAGE	:= dmp/glibc:$(GLIBC_VERSION)
PYTHON_IMAGE	:= dmp/python:$(PYTHON_VERSION_SHORT)-dist
GCC_IMAGE		:= dmp/gcc:$(GCC_VERSION)-dist
LLVM_IMAGE		:= dmp/llvm:$(LLVM_VERSION)-dist
IMAGE			:= dmp/nodejs:$(NODE_VERSION)
IMAGE_SHORT		:= dmp/nodejs:$(NODE_VERSION_MAJOR)
NODE_TARBALL	:= node-v$(NODE_VERSION).tar.xz
NO_CACHE_TEXT	:= $(if $(filter 1,$(NO_CACHE)),--no-cache,)


help:
	@echo "make:"
	@echo "  make all        собрать всё"
	@echo "  make sources    скачать исходники node-v$(NODE_VERSION)"
	@echo "  make dist       собрать промежуточный образ-дистрибутив $(IMAGE)-dist"
	@echo "  make build      собрать образ $(IMAGE) (сборщик: $(BASE_IMAGE), python: $(PYTHON_IMAGE))"
	@echo "  make test       проверить образ $(IMAGE): node, npm, npx, запуск скриптов"
	@echo ""
	@echo "  node собирается из исходников (в комплекте node+npm+npx)."
	@echo "  Рантайм — $(RUNTIME_IMAGE); libstdc++/libgcc_s берутся из $(GCC_IMAGE)."
	@echo ""


debug:
	@echo BASE_IMAGE: $(BASE_IMAGE)
	@echo RUNTIME_IMAGE: $(RUNTIME_IMAGE)
	@echo PYTHON_IMAGE: $(PYTHON_IMAGE)
	@echo GCC_IMAGE: $(GCC_IMAGE)
	@echo IMAGE: $(IMAGE)
	@echo IMAGE_SHORT: $(IMAGE_SHORT)
	@echo NODE_TARBALL: $(NODE_TARBALL)


sources:
	@mkdir -p $(ARTIFACTS)/src
	@dest="$(ARTIFACTS)/src/$(NODE_TARBALL)"; \
	if [ -f "$$dest" ]; then echo "  ✓ $$dest"; \
	else url="https://nodejs.org/dist/v$(NODE_VERSION)/$(NODE_TARBALL)"; \
	  echo ">>> $$url"; curl -fSL -o "$$dest" "$$url"; fi


dist:
	docker build -f Dockerfile --target dist $(NO_CACHE_TEXT) \
		--build-arg BASE_IMAGE=$(BASE_IMAGE) \
		--build-arg RUNTIME_IMAGE=$(RUNTIME_IMAGE) \
		--build-arg PYTHON_IMAGE=$(PYTHON_IMAGE) \
		--build-arg GCC_IMAGE=$(GCC_IMAGE) \
		--build-arg LLVM_IMAGE=$(LLVM_IMAGE) \
		--build-arg NODE_VERSION=$(NODE_VERSION) \
		--tag $(IMAGE)-dist \
		--tag $(IMAGE_SHORT)-dist \
		--progress plain $(BUILD_CONTEXT)


build:
	docker build -f Dockerfile $(NO_CACHE_TEXT) \
		--build-arg BASE_IMAGE=$(BASE_IMAGE) \
		--build-arg RUNTIME_IMAGE=$(RUNTIME_IMAGE) \
		--build-arg PYTHON_IMAGE=$(PYTHON_IMAGE) \
		--build-arg GCC_IMAGE=$(GCC_IMAGE) \
		--build-arg LLVM_IMAGE=$(LLVM_IMAGE) \
		--build-arg NODE_VERSION=$(NODE_VERSION) \
		--tag $(IMAGE) \
		--tag $(IMAGE_SHORT) \
		--progress plain $(BUILD_CONTEXT)


test:
	docker run --rm --entrypoint /bin/bash -v $(TEST_DIR):/test:ro $(IMAGE) -euc '\
		echo "== versions =="; \
		node --version; \
		npm --version; \
		npx --version; \
		echo "== which =="; \
		command -v node; command -v npm; command -v npx; \
		echo "== run hello.js =="; \
		node /test/hello.js; \
		echo "== functional checks =="; \
		node /test/nodecheck.js; \
		echo "== npm offline (init + local link) =="; \
		mkdir -p /tmp/demo && cp -r /test/demo/. /tmp/demo/ && cd /tmp/demo \
		&& npm --offline run start'


all: sources dist build test
