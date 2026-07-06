BASE_DIR      	?= .
ARTIFACTS     	?= $(BASE_DIR)/artifacts
BUILD_CONTEXT 	?= $(BASE_DIR)
TEST_DIR		?= $(BASE_DIR)/test


.DEFAULT_GOAL := help
.PHONY: help debug sources chain dist build test all

GLIBC_VERSION	?= 2.28
GCC_VERSION		?= 8.5.0
GO_VERSION		?= 1.24.5
NO_CACHE		?= 0

GO_CHAIN		?= 1.4 1.17.13 1.20.6 1.22.6 1.24.5
GO_SRC_BASE		?= https://github.com/golang/go/archive/refs/tags

BUILD_IMAGE		:= dmp/gcc:$(GCC_VERSION)
RUNTIME_IMAGE	:= dmp/glibc:$(GLIBC_VERSION)
IMAGE			:= dmp/golang:$(GO_VERSION)
NO_CACHE_TEXT	:= $(if $(filter 1,$(NO_CACHE)),--no-cache,)
GO_BOOTSTRAP	= $(shell echo "$(GO_CHAIN)" | awk -v v='$(GO_VERSION)' '{for(i=1;i<=NF;i++){if($$i==v){print p;exit};p=$$i}}')


help:
	@echo "make:"
	@echo "  make all        собрать всё"
	@echo "  make sources    скачать исходники Go для цепочки вплоть до $(GO_VERSION)"
	@echo "  make chain      собрать bootstrap-цепочку одну за другой до $(GO_VERSION)"
	@echo "  make dist       синоним chain (итог — dmp/golang:$(GO_VERSION)-dist)"
	@echo "  make build      собрать образ $(IMAGE) (рантайм: $(RUNTIME_IMAGE))"
	@echo "  make test       проверить образ $(IMAGE): go version, сборка и запуск hello.go"
	@echo ""
	@echo "  Цепочка: $(GO_CHAIN)"
	@echo "  Другая версия: make all GO_VERSION=1.22.6"
	@echo ""


debug:
	@echo BUILD_IMAGE: $(BUILD_IMAGE)
	@echo RUNTIME_IMAGE: $(RUNTIME_IMAGE)
	@echo IMAGE: $(IMAGE)
	@echo GO_CHAIN: $(GO_CHAIN)
	@echo GO_VERSION: $(GO_VERSION)
	@echo GO_BOOTSTRAP: $(GO_BOOTSTRAP)
	@echo NO_CACHE_TEXT: $(NO_CACHE_TEXT)


sources:
	@mkdir -p $(ARTIFACTS)/src
	@set -e; for v in $(GO_CHAIN); do \
	  dest="$(ARTIFACTS)/src/golang-$$v.tar.gz"; \
	  url="$(GO_SRC_BASE)/go$$v.tar.gz"; \
	  if [ -f "$$dest" ]; then echo "  ✓ $$dest"; \
	  else echo ">>> $$url"; curl -fSL -o "$$dest" "$$url"; fi; \
	  [ "$$v" = "$(GO_VERSION)" ] && break; \
	done


chain: sources
	@set -e; prev=""; \
	for v in $(GO_CHAIN); do \
	  if [ "$$v" = "1.4" ]; then \
	    echo ">>> golang $$v (C bootstrap)"; \
	    docker build -f Dockerfile.bootstrap --target dist $(NO_CACHE_TEXT) \
	      --build-arg BUILD_IMAGE=$(BUILD_IMAGE) \
	      --build-arg RUNTIME_IMAGE=$(RUNTIME_IMAGE) \
	      --build-arg GO_VERSION=$$v \
	      --tag dmp/golang:$$v-dist \
	      --progress plain $(BUILD_CONTEXT); \
	  else \
	    echo ">>> golang $$v (bootstrap: $$prev)"; \
	    docker build -f Dockerfile --target dist $(NO_CACHE_TEXT) \
	      --build-arg BUILD_IMAGE=$(BUILD_IMAGE) \
	      --build-arg RUNTIME_IMAGE=$(RUNTIME_IMAGE) \
	      --build-arg GO_VERSION=$$v \
	      --build-arg GOLANG_IMAGE=dmp/golang:$$prev-dist \
	      --tag dmp/golang:$$v-dist \
	      --progress plain $(BUILD_CONTEXT); \
	  fi; \
	  prev=$$v; \
	  [ "$$v" = "$(GO_VERSION)" ] && break; \
	done


dist: chain


build: chain
	@set -e; \
	if [ "$(GO_VERSION)" = "1.4" ]; then \
	  docker build -f Dockerfile.bootstrap $(NO_CACHE_TEXT) \
	    --build-arg BUILD_IMAGE=$(BUILD_IMAGE) \
	    --build-arg RUNTIME_IMAGE=$(RUNTIME_IMAGE) \
	    --build-arg GO_VERSION=$(GO_VERSION) \
	    --tag $(IMAGE) \
	    --progress plain $(BUILD_CONTEXT); \
	else \
	  docker build -f Dockerfile $(NO_CACHE_TEXT) \
	    --build-arg BUILD_IMAGE=$(BUILD_IMAGE) \
	    --build-arg RUNTIME_IMAGE=$(RUNTIME_IMAGE) \
	    --build-arg GO_VERSION=$(GO_VERSION) \
	    --build-arg GOLANG_IMAGE=dmp/golang:$(GO_BOOTSTRAP)-dist \
	    --tag $(IMAGE) \
	    --progress plain $(BUILD_CONTEXT); \
	fi


test:
	docker run --rm --entrypoint /bin/bash -v $(TEST_DIR):/test:ro $(IMAGE) -euc '\
		echo "== version =="; \
		go version; \
		echo "GOROOT=$$(go env GOROOT 2>/dev/null || echo $$GOROOT)"; \
		echo "== build & run hello.go =="; \
		export GO111MODULE=off; \
		cp /test/hello.go /tmp/hello.go; \
		go build -o /tmp/hello /tmp/hello.go; \
		/tmp/hello'


all: sources chain build test
