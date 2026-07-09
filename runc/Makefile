BASE_DIR      	?= $(abspath .)
ARTIFACTS     	?= $(BASE_DIR)/artifacts
BUILD_CONTEXT 	?= $(BASE_DIR)
TEST_DIR		?= $(BASE_DIR)/test


.DEFAULT_GOAL := help

# список версий, которые поддерживает сборщик
# версии собираются одна за другой, строго по цепочке
VERSION_CHAIN		?= 1.1.12 1.1.15 1.2.6
# целевая версия сборки. по умолчанию берется последняя версия
VERSION      		?= $(lastword $(VERSION_CHAIN))
# базовый url адрес для скачивания исходников собираемых в Dockerfile
BASE_URL			?= https://github.com/opencontainers/runc/archive/refs/tags
# выполнять ли сброс кэша: docker build --no-cache
NO_CACHE			?= 0

GLIBC_IMAGE			?= dmp/glibc:2.28
GOLANG_IMAGE		?= dmp/golang:1.22.6

# зависимости для сборки разных версии
# версия        runtime_image     build_image
DEPS_1.1.12	:= $(GLIBC_IMAGE)    $(GOLANG_IMAGE)
DEPS_1.1.15	:= $(GLIBC_IMAGE)    $(GOLANG_IMAGE)
DEPS_1.2.6	:= $(GLIBC_IMAGE)    $(GOLANG_IMAGE)

# базовое название для образа
IMAGE			:= dmp/runc
FIRST_VERSION	:= $(firstword $(VERSION_CHAIN))
NO_CACHE_TEXT	:= $(if $(filter 1,$(NO_CACHE)),--no-cache,)


# helper: поиск предыдущей версии в VERSION_CHAIN
prev_of = $(shell echo "$(VERSION_CHAIN)" | awk -v v='$(1)' '{for(i=1;i<=NF;i++){if($$i==v){print p;exit};p=$$i}}')


.PHONY: help
help:
	@echo "make:"
	@echo "  make all        собрать всё"
	@echo "  make sources    скачать исходники для цепочки вплоть до $(VERSION)"
	@echo "  make dist       собрать цепочку -dist образов одну за другой до $(VERSION)"
	@echo "  make build      собрать образ $(IMAGE)"
	@echo "  make test       проверить образ $(IMAGE): runc --version / features"
	@echo ""
	@echo "  Цепочка: $(VERSION_CHAIN)"
	@echo "  runc собирается go из dmp/golang с cgo+libseccomp (BUILDTAGS=seccomp)."
	@echo "  Зависимости (runtime_image/build_image) задаются пословно в DEPS_<версия>."
	@echo "  Другая версия: make all VERSION=1.2.6"
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
	$(info IMAGE          = $(IMAGE):$(VERSION))
	$(info DEPS($(VERSION)) = $(DEPS_$(VERSION)))
	@true


# Скачиваем зависимости для сборки дистрибутива
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
	[ -f "$(ARTIFACTS)/src/runc-$$current.tar.gz" ] || curl -v -L -o "$(ARTIFACTS)/src/runc-$$current.tar.gz" "$(BASE_URL)/v$$current.tar.gz"


# docker build --target dist - дистрибутив с скомпилированными исходниками (без установки в систему)
.PHONY: dist
dist: dist-$(VERSION)

.ONESHELL:
dist-%: sources-%
	@current=$(*)
	prev='$(call prev_of,$*)'
	# рекурсивно собираем предыдущую версию
	[ -n "$$prev" ] && $(MAKE) dist-$$prev || true
	set -e
	set -- $(DEPS_$*)
	docker build -f Dockerfile $(NO_CACHE_TEXT) --target dist \
		--build-arg RUNTIME_IMAGE=$$1 			\
		--build-arg BUILD_IMAGE=$$2 			\
		--build-arg RUNC_VERSION=$$current 		\
		--tag $(IMAGE):$$current-dist 			\
		--progress plain $(BUILD_CONTEXT)


# docker build - истоговый образ с установленным ПО (установка из дистрибутива)
.PHONY: build
build: build-$(VERSION)

.ONESHELL:
build-%: dist-%
	@current=$(*)
	prev='$(call prev_of,$*)'
	# рекурсивно собираем предыдущую версию
	[ -n "$$prev" ] && $(MAKE) build-$$prev || true
	set -e
	set -- $(DEPS_$*)
	docker build -f Dockerfile $(NO_CACHE_TEXT) \
		--build-arg RUNTIME_IMAGE=$$1			\
		--build-arg BUILD_IMAGE=$$2				\
		--build-arg RUNC_VERSION=$$current		\
		--tag $(IMAGE):$$current				\
		--progress plain $(BUILD_CONTEXT)


# тестирование собранного образа
.PHONY: test
test: test-$(VERSION)

.ONESHELL:
test-%: build-%
	@current=$(*)
	docker run --rm --entrypoint /bin/bash -v $(TEST_DIR):/test:ro $(IMAGE):$$current -euc '\
		echo "== version ==";					\
		runc --version; 						\
		echo "== features =="; 					\
		runc features | head -c 400; echo; 		\
		echo "== spec (генерация config.json) =="; \
		mkdir -p /tmp/bundle && cd /tmp/bundle && runc spec && test -f config.json && echo "spec ok"'


.PHONY: clean
clean: clean-$(VERSION)

.ONESHELL:
clean-%:
	@current=$(*)
	@prev='$(call prev_of,$*)'
	# вызываем рекурсивно предыдущую версию
	[ -n "$$prev" ] && $(MAKE) clean-$$prev || true
	set -e
	rm -f "$(ARTIFACTS)/src/runc-$$current.tar.gz"


.PHONY: all
all: sources dist build test
