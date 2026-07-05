SRC_DIR       ?= .
ARTIFACTS     ?= artifacts
BUILD_CONTEXT ?= .

.DEFAULT_GOAL := help
.PHONY: help sources dist build all

ASTRA_VERSION  ?= 2.12
GLIBC_VERSION  ?= 2.28
GCC_VERSION      ?= 8.5.0
PYTHON_VERSION   ?= 3.11.15
PYTHON_VERSION_SHORT ?= $(PYTHON_VERSION:0:3)

help:
	@echo "make:"
	@echo "  make all        	скачать всё для сборки и собрать образ astra-linux:$(ASTRA_VERSION)-glibc$(GLIBC_VERSION)-gcc$(GCC_VERSION)-python$(PYTHON_VERSION)"
	@echo "  make sources    	скачать всё для сборки: тарболы python"
	@echo "  make dist   		собрать промежуточный образ astra-linux:$(ASTRA_VERSION)-glibc$(GLIBC_VERSION)-gcc$(GCC_VERSION)-python$(PYTHON_VERSION)-dist"
	@echo "  make build      	собрать образ astra-linux:$(ASTRA_VERSION)-glibc$(GLIBC_VERSION)-gcc$(GCC_VERSION)-python$(PYTHON_VERSION)"
	@echo ""


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
	docker build -f Dockerfile --target dist \
		--build-arg BASE_IMAGE=astra-linux:$(ASTRA_VERSION)-glibc$(GLIBC_VERSION) \
		--build-arg BUILD_IMAGE=astra-linux:$(ASTRA_VERSION)-glibc$(GLIBC_VERSION)-gcc$(GCC_VERSION) \
		--build-arg PYTHON_VERSION=$(PYTHON_VERSION) \
		--build-arg PYTHON_VERSION_SHORT=$(PYTHON_VERSION_SHORT) \
		--tag astra-linux:$(ASTRA_VERSION)-glibc$(GLIBC_VERSION)-python$(PYTHON_VERSION)-dist \
		--tag astra-linux:$(ASTRA_VERSION)-glibc$(GLIBC_VERSION)-python$(PYTHON_VERSION_SHORT)-dist \
		--progress plain $(BUILD_CONTEXT)

build:
	docker build -f Dockerfile \
		--build-arg BASE_IMAGE=astra-linux:$(ASTRA_VERSION)-glibc$(GLIBC_VERSION) \
		--build-arg BUILD_IMAGE=astra-linux:$(ASTRA_VERSION)-glibc$(GLIBC_VERSION)-gcc$(GCC_VERSION) \
		--build-arg PYTHON_VERSION=$(PYTHON_VERSION) \
		--build-arg PYTHON_VERSION_SHORT=$(PYTHON_VERSION_SHORT) \
		--tag astra-linux:$(ASTRA_VERSION)-glibc$(GLIBC_VERSION)-python$(PYTHON_VERSION) \
		--tag astra-linux:$(ASTRA_VERSION)-glibc$(GLIBC_VERSION)-python$(PYTHON_VERSION_SHORT) \
		--progress plain $(BUILD_CONTEXT)

all: sources dist build
