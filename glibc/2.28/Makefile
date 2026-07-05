BASE_DIR      	?= .
ARTIFACTS    	?= $(BASE_DIR)/artifacts
BUILD_CONTEXT	?= .

ASTRA_VERSION	?= 2.12
GLIBC_VERSION	?= 2.28
NO_CACHE		?= 0

OREL_IMAGE    	:= registry.astralinux.ru/library/orel:$(ASTRA_VERSION)
OREL_TAR      	:= $(ARTIFACTS)/orel-$(ASTRA_VERSION).tar
IMAGE         	:= dmp/glibc:$(GLIBC_VERSION)
APT_DEB_DIR   	:= $(ARTIFACTS)/apt-bootstrap
DEB_PACKAGES  	:= apt-transport-https
NO_CACHE_TEXT	:= $(if $(filter 1,$(NO_CACHE)),--no-cache,)

.DEFAULT_GOAL := help
.PHONY: help sources image debs build test all

help:
	@echo "make:"
	@echo "  make all       собрать всё"
	@echo "  make sources   скачать всё для сборки"
	@echo "  make image  	скачать образ astra-linux с офф. репозитория $(OREL_IMAGE) -> $(OREL_TAR)"
	@echo "  make debs      скачать .deb ($(DEB_PACKAGES)) через orel -> $(APT_DEB_DIR)/"
	@echo "  make build     собрать образ $(IMAGE)"
	@echo "  make test      проверить образ $(IMAGE): версия glibc, заголовки, kernel-заголовки, crt, защита от 2.24"
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
	@docker build -f Dockerfile --target glibc $(NO_CACHE_TEXT) \
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

test:
	@docker run --rm -v $(CURDIR)/test:/test:ro $(IMAGE) bash -euc '\
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
		echo "== 6. apt install НЕ откатывает заголовки на 2.24 =="; \
		apt-get update >/dev/null 2>&1; \
		apt-get install -y --no-install-recommends build-essential zlib1g-dev >/dev/null 2>&1; \
		grep "define\s*__GLIBC_MINOR__" /usr/include/features.h; \
		echo "== 7. компиляция и запуск проверочной программы =="; \
		gcc -Wall -O2 /test/glibc_check.c -o /tmp/glibc_check; \
		/tmp/glibc_check'

all: sources debs build test image

