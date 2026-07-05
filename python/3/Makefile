BASE_DIR      ?= .
ARTIFACTS     ?= $(BASE_DIR)/artifacts
BUILD_CONTEXT ?= .

.DEFAULT_GOAL := help
.PHONY: help sources dist build all

ASTRA_VERSION  			?= 2.12
GLIBC_VERSION  			?= 2.28
GCC_VERSION      		?= 8.5.0
PYTHON_VERSION   		?= 3.11.15
BASE_IMAGE				?= dmp/glibc:$(GLIBC_VERSION)
BUILD_IMAGE				?= dmp/gcc:$(GCC_VERSION)
NO_CACHE				?= 0

VERSION_WORDS			:= $(subst ., ,$(PYTHON_VERSION))
PYTHON_VERSION_SHORT	:= $(word 1,$(VERSION_WORDS)).$(word 2,$(VERSION_WORDS))
IMAGE					:= dmp/python:$(PYTHON_VERSION)
IMAGE_SHORT				:= dmp/python:$(PYTHON_VERSION_SHORT)
NO_CACHE_TEXT			:= $(if $(filter 1,$(NO_CACHE)),--no-cache,)

APT_DEPS         		:= \
		zlib1g-dev libncurses5-dev libgdbm-dev libnss3-dev \
        libssl-dev libreadline-dev libffi-dev libsqlite3-dev \
        libbz2-dev liblzma-dev curl


help:
	@echo "make:"
	@echo "  make all        	собрать все"
	@echo "  make sources    	скачать всё для сборки"
	@echo "  make dist   		собрать промежуточный образ-дистрибутив $(IMAGE)-dist"
	@echo "  make build      	собрать образ $(IMAGE)"
	@echo ""


debug:
	@echo PYTHON_VERSION: $(PYTHON_VERSION)
	@echo PYTHON_VERSION_SHORT: $(PYTHON_VERSION_SHORT)
	@echo NO_CACHE_TEXT: $(NO_CACHE_TEXT)
	@echo APT_DEPS: $(APT_DEPS)


sources:
	@mkdir -p $(ARTIFACTS)/src
	@set -e; for spec in \
	  "$(ARTIFACTS)/src/Python-$(PYTHON_VERSION).tar.xz=https://www.python.org/ftp/python/$(PYTHON_VERSION)/Python-$(PYTHON_VERSION).tar.xz" \
	; do \
	  dest="$${spec%%=*}"; url="$${spec#*=}"; \
	  if [ -f "$$dest" ]; then echo "  ✓ $$dest"; \
	  else echo ">>> $$url"; curl -fSL -o "$$dest" "$$url"; fi; \
	done


dist:
	docker build -f Dockerfile --target dist $(NO_CACHE_TEXT) \
		--build-arg BASE_IMAGE=$(BASE_IMAGE) \
		--build-arg BUILD_IMAGE=$(BUILD_IMAGE) \
		--build-arg PYTHON_VERSION=$(PYTHON_VERSION) \
		--build-arg PYTHON_VERSION_SHORT=$(PYTHON_VERSION_SHORT) \
		--build-arg APT_DEPS="$(APT_DEPS)" \
		--tag $(IMAGE)-dist \
		--tag $(IMAGE_SHORT)-dist \
		--progress plain $(BUILD_CONTEXT)


build:
	docker build -f Dockerfile $(NO_CACHE_TEXT) \
		--build-arg BASE_IMAGE=$(BASE_IMAGE) \
		--build-arg BUILD_IMAGE=$(BUILD_IMAGE) \
		--build-arg PYTHON_VERSION=$(PYTHON_VERSION) \
		--build-arg PYTHON_VERSION_SHORT=$(PYTHON_VERSION_SHORT) \
		--build-arg APT_DEPS="$(APT_DEPS)" \
		--tag $(IMAGE) \
		--tag $(IMAGE_SHORT) \
		--progress plain $(BUILD_CONTEXT)


run:
	docker run -f Dockerfile $(NO_CACHE_TEXT) \
		--tag $(IMAGE) \
		--tag $(IMAGE_SHORT) \
		--progress plain $(BUILD_CONTEXT)

all: sources dist build
