ARTIFACTS     ?= artifacts
BUILD_CONTEXT ?= .

ASTRA_VERSION ?= 2.12
GLIBC_VERSION ?= 2.28

OREL_IMAGE    ?= registry.astralinux.ru/library/orel:$(ASTRA_VERSION)
OREL_TAR      ?= $(ARTIFACTS)/orel-$(ASTRA_VERSION).tar
IMAGE         ?= dmp/glibc:$(GLIBC_VERSION)

APT_DEB_DIR   ?= $(ARTIFACTS)/apt-bootstrap
DEB_PACKAGES  ?= apt-transport-https

.DEFAULT_GOAL := help
.PHONY: help sources image debs build

help:
	@echo "make:"
	@echo "  make all       собрать всё"
	@echo "  make sources   скачать всё для сборки"
	@echo "  make image  	скачать образ astra-linux с офф. репозитория $(OREL_IMAGE) -> $(OREL_TAR)"
	@echo "  make debs      скачать .deb ($(DEB_PACKAGES)) через orel -> $(APT_DEB_DIR)/"
	@echo "  make build     собрать образ $(IMAGE)"
	@echo ""


sources:
	@mkdir -p $(ARTIFACTS)/glibc-src
	@dest="$(ARTIFACTS)/glibc-src/glibc-$(GLIBC_VERSION).tar.xz"; \
	if [ -f "$$dest" ]; then echo "  ✓ $$dest"; \
	else url="https://ftp.gnu.org/gnu/glibc/glibc-$(GLIBC_VERSION).tar.xz"; \
	  echo ">>> $$url"; curl -fSL -o "$$dest" "$$url"; fi
	@if [ -f "$(OREL_TAR)" ]; then echo "  ✓ $(OREL_TAR)"; \
	else echo "  ! нет $(OREL_TAR) — положи tar-архив базового образа astra в $(ARTIFACTS)/ вручную"; fi

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

build:
	@docker build -f Dockerfile --target glibc \
		--build-arg BASE_IMAGE=$(OREL_IMAGE) \
		--build-arg GLIBC_VERSION=$(GLIBC_VERSION) \
		--tag $(IMAGE) \
		--progress plain $(BUILD_CONTEXT)
	@docker images $(IMAGE)

image:
	@mkdir -p $(ARTIFACTS)
	@if [ -f "$(OREL_TAR)" ]; then echo "  ✓ $(OREL_TAR)"; \
	else echo ">>> docker pull $(OREL_IMAGE)"; docker pull $(OREL_IMAGE); \
	  echo ">>> docker save -> $(OREL_TAR)"; docker save -o "$(OREL_TAR)" $(OREL_IMAGE); \
	  echo "  ✓ $(OREL_TAR)"; fi

all: sources debs build image

