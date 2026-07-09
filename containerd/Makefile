BASE_DIR      	?= $(abspath .)
ARTIFACTS     	?= $(BASE_DIR)/artifacts
BUILD_CONTEXT 	?= $(BASE_DIR)
TEST_DIR		?= $(BASE_DIR)/test


.DEFAULT_GOAL := help

# список версий, которые поддерживает сборщик
# версии собираются одна за другой, строго по цепочке
VERSION_CHAIN		?= 1.6.38 1.7.27 2.0.5
# целевая версия сборки. по умолчанию берется последняя версия
VERSION      		?= $(lastword $(VERSION_CHAIN))
# базовый url адрес для скачивания исходников собираемых в Dockerfile
BASE_URL			?= https://github.com/containerd/containerd/archive/refs/tags
# выполнять ли сброс кэша: docker build --no-cache
NO_CACHE			?= 0

GLIBC_IMAGE			?= dmp/glibc:2.28
GOLANG_IMAGE		?= dmp/golang:1.22.6
GOLANG124_IMAGE		?= dmp/golang:1.24.5

# зависимости для сборки разных версии
# версия        runtime_image     build_image
DEPS_1.6.38	:= $(GLIBC_IMAGE)    $(GOLANG_IMAGE)
DEPS_1.7.27	:= $(GLIBC_IMAGE)    $(GOLANG_IMAGE)
DEPS_2.0.5	:= $(GLIBC_IMAGE)    $(GOLANG124_IMAGE)

# базовое название для образа
IMAGE			:= dmp/containerd
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
	@echo "  make test       проверить образ $(IMAGE): containerd/ctr --version"
	@echo ""
	@echo "  Цепочка: $(VERSION_CHAIN)"
	@echo "  containerd собирается go из dmp/golang статически (CGO_ENABLED=0, no_btrfs)."
	@echo "  Бинарники: containerd, containerd-shim-runc-v2, ctr."
	@echo "  Зависимости (runtime_image/build_image) задаются пословно в DEPS_<версия>."
	@echo "  Другая версия: make all VERSION=1.7.27"
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
	[ -f "$(ARTIFACTS)/src/containerd-$$current.tar.gz" ] || curl -v -L -o "$(ARTIFACTS)/src/containerd-$$current.tar.gz" "$(BASE_URL)/v$$current.tar.gz"


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
		--build-arg RUNTIME_IMAGE=$$1 				\
		--build-arg BUILD_IMAGE=$$2 				\
		--build-arg CONTAINERD_VERSION=$$current 	\
		--tag $(IMAGE):$$current-dist 				\
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
	docker build -f Dockerfile $(NO_CACHE_TEXT) 	\
		--build-arg RUNTIME_IMAGE=$$1				\
		--build-arg BUILD_IMAGE=$$2					\
		--build-arg CONTAINERD_VERSION=$$current	\
		--tag $(IMAGE):$$current					\
		--progress plain $(BUILD_CONTEXT)


# тестирование собранного образа
.PHONY: test
test: test-$(VERSION)

.ONESHELL:
test-%: build-%
	@current=$(*)
	docker run --rm --entrypoint /bin/bash -v $(TEST_DIR):/test:ro $(IMAGE):$$current -euc '\
		echo "== versions ==";						\
		containerd --version; 						\
		ctr --version; 								\
		echo "== which =="; 						\
		command -v containerd; command -v ctr; command -v containerd-shim-runc-v2; \
		echo "== containerd config default =="; 	\
		containerd config default | head -c 200; echo'


.PHONY: clean
clean: clean-$(VERSION)

.ONESHELL:
clean-%:
	@current=$(*)
	@prev='$(call prev_of,$*)'
	# вызываем рекурсивно предыдущую версию
	[ -n "$$prev" ] && $(MAKE) clean-$$prev || true
	set -e
	rm -f "$(ARTIFACTS)/src/containerd-$$current.tar.gz"


.PHONY: all
all: sources dist build test
