BASE_DIR      	?= $(abspath .)
ARTIFACTS     	?= $(BASE_DIR)/artifacts
BUILD_CONTEXT 	?= $(BASE_DIR)
TEST_DIR		?= $(BASE_DIR)/test


.DEFAULT_GOAL := help

# список версий, которые поддерживает сборщик
# версии redis независимы, собираются отдельными целями
VERSIONS			?= 6.2.16 7.0.15 7.2.7 7.4.2
# целевая версия сборки. по умолчанию берется последняя версия
VERSION      		?= $(lastword $(VERSIONS))
# базовый url адрес для скачивания исходников собираемых в Dockerfile
BASE_URL			?= https://download.redis.io/releases
# выполнять ли сброс кэша: docker build --no-cache
NO_CACHE			?= 0
# собирать redis с поддержкой TLS (требует libssl-dev)
BUILD_TLS			?= yes

# apt-deps (dev) для сборки сервера
APT_DEPS			?= pkg-config libssl-dev
# apt-deps (runtime) — .so, против которых слинкован сервер
RUNTIME_APT_DEPS	?= libssl1.1 ca-certificates tzdata

GLIBC_IMAGE			?= dmp/glibc:2.28
GCC_IMAGE			?= dmp/gcc:8.5.0-dist

# зависимости для сборки разных версии
# версия           runtime_image      build_image
DEPS_6.2.16		:= $(GLIBC_IMAGE)    $(GCC_IMAGE)
DEPS_7.0.15		:= $(GLIBC_IMAGE)    $(GCC_IMAGE)
DEPS_7.2.7		:= $(GLIBC_IMAGE)    $(GCC_IMAGE)
DEPS_7.4.2		:= $(GLIBC_IMAGE)    $(GCC_IMAGE)

# базовое название для образа
IMAGE			:= dmp/redis
NO_CACHE_TEXT	:= $(if $(filter 1,$(NO_CACHE)),--no-cache,)


.PHONY: help
help:
	@echo "make:"
	@echo "  make all        собрать всё (перебор VERSIONS)"
	@echo "  make sources    скачать исходники redis версии $(VERSION)"
	@echo "  make dist       собрать -dist образ версии $(VERSION)"
	@echo "  make build      собрать образ $(IMAGE):$(VERSION)"
	@echo "  make test       проверить образ: старт сервера, ping/set/get"
	@echo ""
	@echo "  Версии: $(VERSIONS)"
	@echo "  Каждая версия собирается тулчейном $(GCC_IMAGE), рантайм — $(GLIBC_IMAGE)."
	@echo "  Другая версия: make build VERSION=7.2.7"
	@echo ""


.PHONY: debug
debug:
	$(info BASE_DIR         = $(BASE_DIR))
	$(info ARTIFACTS        = $(ARTIFACTS))
	$(info BUILD_CONTEXT    = $(BUILD_CONTEXT))
	$(info TEST_DIR         = $(TEST_DIR))
	$(info VERSIONS         = $(VERSIONS))
	$(info VERSION          = $(VERSION))
	$(info BASE_URL         = $(BASE_URL))
	$(info NO_CACHE         = $(NO_CACHE))
	$(info NO_CACHE_TEXT    = $(NO_CACHE_TEXT))
	$(info BUILD_TLS        = $(BUILD_TLS))
	$(info APT_DEPS         = $(APT_DEPS))
	$(info RUNTIME_APT_DEPS = $(RUNTIME_APT_DEPS))
	$(info IMAGE            = $(IMAGE):$(VERSION))
	$(info DEPS($(VERSION)) = $(DEPS_$(VERSION)))
	@true


# redis sources download
.PHONY: sources
sources: sources-$(VERSION)

.ONESHELL:
sources-%:
	@current=$(*)
	set -e
	mkdir -p $(ARTIFACTS)/src
	[ -f "$(ARTIFACTS)/src/redis-$$current.tar.gz" ] || curl -v -L -o "$(ARTIFACTS)/src/redis-$$current.tar.gz" \
		"$(BASE_URL)/redis-$$current.tar.gz"


# docker build --target dist - дистрибутив с скомпилированным сервером (без установки в систему)
.PHONY: dist
dist: dist-$(VERSION)

.ONESHELL:
dist-%: sources-%
	@current=$(*)
	set -e
	set -- $(DEPS_$*)
	runtime=$$1; build=$$2
	short=$${current%.*}
	docker build -f Dockerfile $(NO_CACHE_TEXT) --target dist	\
		--build-arg RUNTIME_IMAGE=$$runtime 					\
		--build-arg BUILD_IMAGE=$$build 						\
		--build-arg REDIS_VERSION=$$current 					\
		--build-arg BUILD_TLS=$(BUILD_TLS) 						\
		--build-arg APT_DEPS="$(APT_DEPS)" 						\
		--tag $(IMAGE):$$current-dist 							\
		--tag $(IMAGE):$$short-dist 							\
		--progress plain $(BUILD_CONTEXT)


# docker build - истоговый образ с установленным сервером (установка из дистрибутива)
.PHONY: build
build: build-$(VERSION)

.ONESHELL:
build-%: dist-%
	@current=$(*)
	set -e
	set -- $(DEPS_$*)
	runtime=$$1; build=$$2
	short=$${current%.*}
	docker build -f Dockerfile $(NO_CACHE_TEXT) 				\
		--build-arg RUNTIME_IMAGE=$$runtime 					\
		--build-arg BUILD_IMAGE=$$build 						\
		--build-arg REDIS_VERSION=$$current 					\
		--build-arg BUILD_TLS=$(BUILD_TLS) 						\
		--build-arg APT_DEPS="$(APT_DEPS)" 						\
		--build-arg RUNTIME_APT_DEPS="$(RUNTIME_APT_DEPS)" 		\
		--tag $(IMAGE):$$current 								\
		--tag $(IMAGE):$$short 									\
		--progress plain $(BUILD_CONTEXT)


# тестирование собранного образа
.PHONY: test
test: test-$(VERSION)

.ONESHELL:
test-%: build-%
	@current=$(*)
	docker run --rm --entrypoint /bin/bash -v $(TEST_DIR):/test:ro $(IMAGE):$$current /test/redischeck.sh


.PHONY: clean
clean: clean-$(VERSION)

.ONESHELL:
clean-%:
	@current=$(*)
	set -e
	rm -f "$(ARTIFACTS)/src/redis-$$current.tar.gz"


.PHONY: all
all:
	@set -e
	for v in $(VERSIONS); do $(MAKE) test-$$v; done
