BASE_DIR      	?= $(abspath .)
ARTIFACTS     	?= $(BASE_DIR)/artifacts
BUILD_CONTEXT 	?= $(BASE_DIR)
TEST_DIR		?= $(BASE_DIR)/test


.DEFAULT_GOAL := help

# список версий, которые поддерживает сборщик
# версии собираются одна за другой, строго по цепочке
VERSION_CHAIN		?= 2.28 2.29 2.30 2.31 2.32 2.33 2.34 2.35 2.36 2.37 2.38 2.39 2.40 2.41 2.42 2.43
# целевая версия сборки. по умолчанию берётся последняя версия
# соответственно для её сборки потребуется собрать все предыдущие версии в цепочке
VERSION      		?= $(lastword $(VERSION_CHAIN))
# базовый url адрес для скачивания исходников собираемых в Dockerfile
BASE_URL			?= https://ftp.gnu.org/gnu
# выполнять ли сброс кэша: docker build --no-cache
NO_CACHE			?= 0

# базова для сборки
ASTRA_VERSION		?= 2.12
OREL_IMAGE			:= registry.astralinux.ru/library/orel:$(ASTRA_VERSION)
PYTHON_IMAGE		?= dmp/python:3.10-dist
GCC_IMAGE			?= dmp/gcc:8.5.0-dist


# зависимости для сборки разных версий
# версия           runtime_image        python_image        gcc_image
DEPS_2.28		:= $(OREL_IMAGE)        $(OREL_IMAGE)       $(OREL_IMAGE)
DEPS_2.29		:= dmp/glibc:2.28       $(PYTHON_IMAGE)     $(GCC_IMAGE)
DEPS_2.30		:= dmp/glibc:2.29       $(PYTHON_IMAGE)     $(GCC_IMAGE)
DEPS_2.31		:= dmp/glibc:2.30       $(PYTHON_IMAGE)     $(GCC_IMAGE)
DEPS_2.32		:= dmp/glibc:2.31       $(PYTHON_IMAGE)     $(GCC_IMAGE)
DEPS_2.33		:= dmp/glibc:2.32       $(PYTHON_IMAGE)     $(GCC_IMAGE)
DEPS_2.34		:= dmp/glibc:2.33       $(PYTHON_IMAGE)     $(GCC_IMAGE)
DEPS_2.35		:= dmp/glibc:2.34       $(PYTHON_IMAGE)     $(GCC_IMAGE)
DEPS_2.36		:= dmp/glibc:2.35       $(PYTHON_IMAGE)     $(GCC_IMAGE)
DEPS_2.37		:= dmp/glibc:2.36       $(PYTHON_IMAGE)     $(GCC_IMAGE)
DEPS_2.38		:= dmp/glibc:2.37       $(PYTHON_IMAGE)     $(GCC_IMAGE)
DEPS_2.39		:= dmp/glibc:2.38       $(PYTHON_IMAGE)     $(GCC_IMAGE)
DEPS_2.40		:= dmp/glibc:2.39       $(PYTHON_IMAGE)     $(GCC_IMAGE)
DEPS_2.41		:= dmp/glibc:2.40       $(PYTHON_IMAGE)     $(GCC_IMAGE)
DEPS_2.42		:= dmp/glibc:2.41       $(PYTHON_IMAGE)     $(GCC_IMAGE)
DEPS_2.43		:= dmp/glibc:2.42       $(PYTHON_IMAGE)     $(GCC_IMAGE)

# базовое название для образа
IMAGE			:= dmp/glibc
FIRST_VERSION	:= $(firstword $(VERSION_CHAIN))
NO_CACHE_TEXT	:= $(if $(filter 1,$(NO_CACHE)),--no-cache,)

# orel-tar и .deb для закрытого контура (bootstrap первого звена)
OREL_TAR		:= $(ARTIFACTS)/orel-$(ASTRA_VERSION).tar
APT_DEB_DIR		:= $(ARTIFACTS)/apt-bootstrap
DEB_PACKAGES	:= apt-transport-https

# apt-sources.list закрытого контура
# монтируется для цели make deps
DEPS_SRC_MOUNT	:= $(if $(wildcard $(BASE_DIR)/apt-sources.list),-v "$(BASE_DIR)/apt-sources.list":/etc/apt/sources.list:ro,)


# helper: поиск предыдущей версии в VERSION_CHAIN
prev_of = $(shell echo "$(VERSION_CHAIN)" | awk -v v='$(1)' '{for(i=1;i<=NF;i++){if($$i==v){print p;exit};p=$$i}}')


.PHONY: help
help:
	@echo "make:"
	@echo "  make all        собрать всё"
	@echo "  make sources    скачать исходники glibc для цепочки вплоть до $(VERSION)"
	@echo "  make image      скачать образ astra $(OREL_IMAGE) -> $(OREL_TAR)"
	@echo "  make deps       скачать .deb ($(DEB_PACKAGES)) через orel -> $(APT_DEB_DIR)/"
	@echo "  make dist       собрать цепочку -dist образов одну за другой до $(VERSION)"
	@echo "  make build      собрать образ $(IMAGE):$(VERSION)"
	@echo "  make test       проверить образ: версия glibc, заголовки, crt, защита от 2.24"
	@echo ""
	@echo "  Цепочка: $(VERSION_CHAIN)"
	@echo "  Первое звено ($(FIRST_VERSION)) — поверх orel (glibc 2.24)."
	@echo "  Каждое следующее собирается предыдущим glibc как базой,"
	@echo "  компилятор — gcc-образ, python — из python-dist."
	@echo "  Зависимости (runtime_image/python/gcc) задаются пословно в DEPS_<версия>."
	@echo "  Другая версия: make all VERSION=2.28"
	@echo ""


.PHONY: debug
debug:
	$(info BASE_DIR       = $(BASE_DIR))
	$(info ARTIFACTS      = $(ARTIFACTS))
	$(info BUILD_CONTEXT  = $(BUILD_CONTEXT))
	$(info TEST_DIR       = $(TEST_DIR))
	$(info VERSION_CHAIN  = $(VERSION_CHAIN))
	$(info VERSION        = $(VERSION))
	$(info FIRST_VERSION  = $(FIRST_VERSION))
	$(info BASE_URL       = $(BASE_URL))
	$(info NO_CACHE       = $(NO_CACHE))
	$(info NO_CACHE_TEXT  = $(NO_CACHE_TEXT))
	$(info ASTRA_VERSION  = $(ASTRA_VERSION))
	$(info OREL_IMAGE     = $(OREL_IMAGE))
	$(info OREL_TAR       = $(OREL_TAR))
	$(info APT_DEB_DIR    = $(APT_DEB_DIR))
	$(info DEB_PACKAGES   = $(DEB_PACKAGES))
	$(info IMAGE          = $(IMAGE):$(VERSION))
	$(info DEPS($(VERSION)) = $(DEPS_$(VERSION)))
	@true


# Скачиваем исходники glibc для сборки дистрибутива
.PHONY: sources
sources: sources-$(VERSION)

.ONESHELL:
sources-%:
	@current=$(*)
	@prev='$(call prev_of,$*)'
	# вызываем рекурсивно предыдущую версию
	[ -n "$$prev" ] && $(MAKE) sources-$$prev || true
	set -e
	mkdir -p $(ARTIFACTS)/src
	[ -f "$(ARTIFACTS)/src/glibc-$$current.tar.xz" ] || curl -v -O --output-dir "$(ARTIFACTS)/src" "$(BASE_URL)/glibc/glibc-$$current.tar.xz"


# Скачиваем .deb для bootstrap закрытого контура
.PHONY: deps
deps:
	@mkdir -p $(APT_DEB_DIR)
	@docker image inspect $(OREL_IMAGE) >/dev/null 2>&1 || \
	  { if [ -f "$(OREL_TAR)" ]; then echo ">>> docker load -i $(OREL_TAR)"; docker load -i "$(OREL_TAR)"; \
	    else echo "  ! образ $(OREL_IMAGE) не найден — сделай 'make image', затем 'docker load -i $(OREL_TAR)'"; exit 1; fi; }
	docker run --rm 				\
	  -v "$(APT_DEB_DIR)":/out		\
	  $(DEPS_SRC_MOUNT)				\
	  $(OREL_IMAGE)					\
	  bash -euc 'apt-get update && 	\
	    apt-get install -y --reinstall --download-only $(DEB_PACKAGES) && \
	    cp -v /var/cache/apt/archives/*.deb /out/ && \
	    chmod -R a+rwX /out'
	@echo "  ✓ .deb -> $(APT_DEB_DIR)/"
	@ls -1 $(APT_DEB_DIR)/*.deb 2>/dev/null || echo "  ! ни одного .deb не скачано"


# docker build --target dist - дистрибутив со скомпилированными исходниками (без установки в систему)
.PHONY: dist
dist: dist-$(VERSION)

.ONESHELL:
dist-%: sources-%
	@current=$(*)
	prev='$(call prev_of,$*)'
	# base = предыдущий glibc (полный образ), поэтому рекурсивно собираем его целиком
	[ -n "$$prev" ] && $(MAKE) build-$$prev || true
	set -e
	set -- $(DEPS_$*)
	docker build -f Dockerfile $(NO_CACHE_TEXT) --target dist	\
		--build-arg RUNTIME_IMAGE=$$1								\
		--build-arg PYTHON_IMAGE=$$2							\
		--build-arg GCC_IMAGE=$$3								\
		--build-arg GLIBC_VERSION=$$current						\
		--tag $(IMAGE):$$current-dist							\
		--progress plain $(BUILD_CONTEXT)


# docker build - итоговый образ с установленным ПО (установка из дистрибутива)
.PHONY: build
build: build-$(VERSION)

.ONESHELL:
build-%: dist-%
	@current=$(*)
	set -e
	set -- $(DEPS_$*)
	# предыдущий glibc уже собран через dist-% -> build-$$prev
	docker build -f Dockerfile $(NO_CACHE_TEXT) \
		--build-arg RUNTIME_IMAGE=$$1			\
		--build-arg GLIBC_VERSION=$$current		\
		--build-arg PYTHON_IMAGE=$$2			\
		--build-arg GCC_IMAGE=$$3				\
		--tag $(IMAGE):$$current				\
		--progress plain $(BUILD_CONTEXT)


# тестирование собранного образа
.PHONY: test
test: test-$(VERSION)

.ONESHELL:
test-%: build-%
	@current=$(*)
	docker run --rm --entrypoint /bin/bash -v $(TEST_DIR):/test:ro $(IMAGE):$$current -euc '\
		echo "== 1. runtime glibc ==";				\
		ldd --version | head -1; 					\
		getconf GNU_LIBC_VERSION; 					\
		echo "== 2. версия заголовков glibc =="; 	\
		grep "define\s*__GLIBC_MINOR__" /usr/include/features.h; 	\
		echo "== 3. kernel-заголовки (linux-libc-dev) =="; 			\
		ls /usr/include/linux/limits.h /usr/include/x86_64-linux-gnu/asm/unistd.h /usr/include/asm-generic/errno.h; \
		echo "== 4. crt-объекты + libc.so (линковка) =="; 			\
		ls /usr/lib/x86_64-linux-gnu/crt1.o /usr/lib/x86_64-linux-gnu/crti.o /usr/lib/x86_64-linux-gnu/crtn.o /usr/lib/x86_64-linux-gnu/libc.so; \
		echo "== 5. fake libc6-dev на hold =="; 					\
		dpkg -l libc6-dev | tail -1; 								\
		apt-mark showhold | grep -qx libc6-dev && echo "hold: OK" || { echo "hold: FAIL"; exit 1; }; 	\
		echo "== 6. apt install НЕ откатывает заголовки =="; 		\
		apt-get update >/dev/null 2>&1; 							\
		apt-get install -y --no-install-recommends build-essential zlib1g-dev >/dev/null 2>&1; 			\
		grep "define\s*__GLIBC_MINOR__" /usr/include/features.h; 	\
		echo "== 7. компиляция и запуск проверочной программы =="; 	\
		gcc -Wall -O2 /test/glibc_check.c -o /tmp/glibc_check; 		\
		/tmp/glibc_check'


# удаление скачанных исходников по цепочке
.PHONY: clean
clean: clean-$(VERSION)

.ONESHELL:
clean-%:
	@current=$(*)
	@prev='$(call prev_of,$*)'
	# вызываем рекурсивно предыдущую версию
	[ -n "$$prev" ] && $(MAKE) clean-$$prev || true
	set -e
	rm -f "$(ARTIFACTS)/src/glibc-$$current.tar.xz"


.PHONY: all
all: sources deps dist build test
