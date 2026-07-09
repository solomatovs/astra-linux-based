BASE_DIR      	?= $(abspath .)
ARTIFACTS     	?= $(BASE_DIR)/artifacts
BUILD_CONTEXT 	?= $(BASE_DIR)
TEST_DIR		?= $(BASE_DIR)/test


.DEFAULT_GOAL := help

# список версий docker (moby + cli собираются одной версией)
# версии собираются одна за другой, строго по цепочке
VERSION_CHAIN		?= 23.0.15 24.0.9 26.1.5 27.5.1
# целевая версия сборки. по умолчанию берется последняя версия
VERSION      		?= $(lastword $(VERSION_CHAIN))
# базовые url адреса для скачивания исходников (moby = dockerd, cli = docker)
MOBY_URL			?= https://github.com/moby/moby/archive/refs/tags
CLI_URL				?= https://github.com/docker/cli/archive/refs/tags
# выполнять ли сброс кэша: docker build --no-cache
NO_CACHE			?= 0

GLIBC_IMAGE			?= dmp/glibc:2.28
GOLANG_IMAGE		?= dmp/golang:1.22.6

# зависимости для сборки разных версии
# версия        runtime_image     build_image      runc_image                 containerd_image
DEPS_23.0.15	:= $(GLIBC_IMAGE)    $(GOLANG_IMAGE)   dmp/runc:1.1.12-dist    dmp/containerd:1.6.38-dist
DEPS_24.0.9		:= $(GLIBC_IMAGE)    $(GOLANG_IMAGE)   dmp/runc:1.1.15-dist    dmp/containerd:1.7.27-dist
DEPS_26.1.5		:= $(GLIBC_IMAGE)    $(GOLANG_IMAGE)   dmp/runc:1.1.15-dist    dmp/containerd:1.7.27-dist
DEPS_27.5.1		:= $(GLIBC_IMAGE)    $(GOLANG_IMAGE)   dmp/runc:1.2.6-dist     dmp/containerd:1.7.27-dist

# базовое название для образа
IMAGE			:= dmp/docker
FIRST_VERSION	:= $(firstword $(VERSION_CHAIN))
NO_CACHE_TEXT	:= $(if $(filter 1,$(NO_CACHE)),--no-cache,)


# helper: поиск предыдущей версии в VERSION_CHAIN
prev_of = $(shell echo "$(VERSION_CHAIN)" | awk -v v='$(1)' '{for(i=1;i<=NF;i++){if($$i==v){print p;exit};p=$$i}}')


.PHONY: help
help:
	@echo "make:"
	@echo "  make all        собрать всё"
	@echo "  make deps       собрать зависимости: образы runc и containerd (-dist)"
	@echo "  make sources    скачать исходники moby+cli для цепочки вплоть до $(VERSION)"
	@echo "  make dist       собрать цепочку -dist образов одну за другой до $(VERSION)"
	@echo "  make build      собрать образ $(IMAGE) (dockerd + docker CLI + containerd + runc)"
	@echo "  make test       проверить образ $(IMAGE): версии всех бинарников движка"
	@echo ""
	@echo "  Цепочка: $(VERSION_CHAIN)"
	@echo "  dockerd (moby) собирается go с cgo+seccomp, docker CLI (docker/cli) — статически."
	@echo "  runc/containerd подтягиваются из $(RUNC_IMAGE) / $(CONTAINERD_IMAGE)"
	@echo "  через COPY --from — собери их заранее ('make deps' или в папках ../runc, ../containerd)."
	@echo "  Другая версия: make all VERSION=27.5.1"
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
	$(info MOBY_URL       = $(MOBY_URL))
	$(info CLI_URL        = $(CLI_URL))
	$(info NO_CACHE       = $(NO_CACHE))
	$(info NO_CACHE_TEXT  = $(NO_CACHE_TEXT))
	$(info IMAGE          = $(IMAGE):$(VERSION))
	$(info DEPS($(VERSION)) = $(DEPS_$(VERSION)))
	@true


.PHONY: deps
deps:
	$(MAKE) -C $(BASE_DIR)/../runc dist
	$(MAKE) -C $(BASE_DIR)/../containerd dist


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
	[ -f "$(ARTIFACTS)/src/moby-$$current.tar.gz" ] || curl -v -L -o "$(ARTIFACTS)/src/moby-$$current.tar.gz" "$(MOBY_URL)/v$$current.tar.gz"
	[ -f "$(ARTIFACTS)/src/cli-$$current.tar.gz" ] || curl -v -L -o "$(ARTIFACTS)/src/cli-$$current.tar.gz"  "$(CLI_URL)/v$$current.tar.gz"


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
		--build-arg RUNC_IMAGE=$$3 				\
		--build-arg CONTAINERD_IMAGE=$$4 		\
		--build-arg DOCKER_VERSION=$$current 	\
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
	major=$${current%%.*}
	docker build -f Dockerfile $(NO_CACHE_TEXT) \
		--build-arg RUNTIME_IMAGE=$$1			\
		--build-arg BUILD_IMAGE=$$2				\
		--build-arg RUNC_IMAGE=$$3				\
		--build-arg CONTAINERD_IMAGE=$$4		\
		--build-arg DOCKER_VERSION=$$current	\
		--tag $(IMAGE):$$current				\
		--tag $(IMAGE):$$major					\
		--progress plain $(BUILD_CONTEXT)


# тестирование собранного образа
.PHONY: test
test: test-$(VERSION)

.ONESHELL:
test-%: build-%
	@current=$(*)
	docker run --rm --entrypoint /bin/bash -v $(TEST_DIR):/test:ro $(IMAGE):$$current -euc '\
		echo "== versions ==";					\
		dockerd --version; 						\
		docker --version; 						\
		containerd --version; 					\
		runc --version; 						\
		echo "== which =="; 					\
		command -v dockerd; command -v docker; command -v docker-proxy; \
		command -v containerd; command -v containerd-shim-runc-v2; command -v runc; \
		echo "== dockerd --help (smoke) =="; 	\
		dockerd --help >/dev/null && echo "dockerd help ok"'


.PHONY: clean
clean: clean-$(VERSION)

.ONESHELL:
clean-%:
	@current=$(*)
	@prev='$(call prev_of,$*)'
	# вызываем рекурсивно предыдущую версию
	[ -n "$$prev" ] && $(MAKE) clean-$$prev || true
	set -e
	rm -f "$(ARTIFACTS)/src/moby-$$current.tar.gz"
	rm -f "$(ARTIFACTS)/src/cli-$$current.tar.gz"


.PHONY: all
all: sources dist build test
