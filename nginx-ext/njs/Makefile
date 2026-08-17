BASE_DIR      	?= $(abspath .)
ARTIFACTS     	?= $(abspath $(BASE_DIR)/../../../ci-artifacts/nginx-ext/njs)
BUILD_CONTEXT 	?= $(BASE_DIR)
# рабочая копия $(ARTIFACTS) внутри контекста сборки (хардлинки, см. artifacts-link)
LINKED_ARTIFACTS ?= $(BUILD_CONTEXT)/artifacts
TEST_DIR		?= $(BASE_DIR)/test


.DEFAULT_GOAL := help

# имя модуля (оно же суффикс тега образа)
EXT				:= njs
# версии nginx, для которых собирается модуль
VERSIONS		?= 1.24.0 1.26.3 1.27.4 1.28.0
VERSION      	?= $(lastword $(VERSIONS))
# выполнять ли сброс кэша: docker build --no-cache
NO_CACHE		?= 0

IMAGE			:= dmp/nginx

# njs
NJS_VERSION		?= 0.9.0
NJS_URL			?= https://github.com/nginx/njs/archive/refs/tags

NO_CACHE_TEXT	:= $(if $(filter 1,$(NO_CACHE)),--no-cache,)

# FORCE=1 — пересобрать, даже если образ уже есть
FORCE				?= 0

# уже собранные образы, снимок на момент запуска make
BUILT				:= $(if $(filter 1,$(FORCE) $(NO_CACHE)),,$(shell docker images --format '{{.Repository}}:{{.Tag}}' 2>/dev/null))
built = $(filter $(1),$(BUILT))
need_build = $(if $(call built,$(call tag,$(1))),,sources artifacts-link)

# второй проход по зависимостям: только там уже известен $* (версия)
.SECONDEXPANSION:
# зависимости обновляются слева направо, artifacts-link должен отработать последним
.NOTPARALLEL:

.PHONY: build-all check-all test-all
build-all:		$(addprefix build-,$(VERSIONS))
check-all:		$(addprefix check-,$(VERSIONS))
test-all:		$(addprefix test-,$(VERSIONS))

# базовый образ и тег результата. подменяются, чтобы поставить модуль
# поверх образа с другими модулями (цепочка)
BASE_IMAGE		?=
TAG				?=
base_image = $(if $(BASE_IMAGE),$(BASE_IMAGE),$(IMAGE):$(1))
tag        = $(if $(TAG),$(TAG),$(IMAGE):$(1)-$(EXT))


.PHONY: help
help:
	@echo "make ($(EXT)):"
	@echo "  make all        собрать для всех поддерживаемых версий nginx"
	@echo "  make sources    скачать исходники njs"
	@echo "  make build      собрать образ $(call tag,$(VERSION))"
	@echo "  make test       проверить модули в образе"
	@echo ""
	@echo "  Версии nginx: $(VERSIONS)"
	@echo "  Даёт ngx_http_js_module и ngx_stream_js_module (движок njs $(NJS_VERSION))."
	@echo "  Собирается в дереве $(IMAGE):<ver>-dist, ставится поверх $(call base_image,$(VERSION))."
	@echo "  Другая версия: make build VERSION=1.26.3"
	@echo "  Поверх других модулей: make build BASE_IMAGE=$(IMAGE):<ver>-njs TAG=$(IMAGE):<ver>-ext"
	@echo ""


.PHONY: debug
debug:
	$(info EXT           = $(EXT))
	$(info VERSIONS      = $(VERSIONS))
	$(info VERSION       = $(VERSION))
	$(info NJS_VERSION   = $(NJS_VERSION))
	$(info BASE_IMAGE    = $(call base_image,$(VERSION)))
	$(info TAG           = $(call tag,$(VERSION)))
	@true



# docker build видит только свой контекст, поэтому перед сборкой подкладываем
# $(ARTIFACTS) в $(LINKED_ARTIFACTS) хардлинками: места не занимает, копий нет
.PHONY: artifacts-link
artifacts-link:
	@set -e
	if [ ! -d "$(ARTIFACTS)" ]; then
		if [ -n "$$(ls -A '$(LINKED_ARTIFACTS)' 2>/dev/null)" ]; then
			echo "  ! $(ARTIFACTS) не найден, а $(LINKED_ARTIFACTS) не пуст" >&2
			echo "  ! перенеси артефакты в ci-artifacts, тут только рабочая копия" >&2
			exit 1
		fi
		mkdir -p "$(ARTIFACTS)"
	fi
	rm -rf "$(LINKED_ARTIFACTS)"
	cp -al "$(ARTIFACTS)" "$(LINKED_ARTIFACTS)" 2>/dev/null || {
		rm -rf "$(LINKED_ARTIFACTS)"
		cp -a "$(ARTIFACTS)" "$(LINKED_ARTIFACTS)"
	}


# Скачиваем исходники njs
.PHONY: sources
sources:
	@set -e
	mkdir -p $(ARTIFACTS)/src
	[ -f "$(ARTIFACTS)/src/njs-$(NJS_VERSION).tar.gz" ] || curl -v -L -o "$(ARTIFACTS)/src/njs-$(NJS_VERSION).tar.gz" \
		"$(NJS_URL)/$(NJS_VERSION).tar.gz"


# docker build - базовый образ + модули
.PHONY: build
build: build-$(VERSION)

.ONESHELL:
build-%: $$(call need_build,$$*)
	@set -e
	[ -z "$(call built,$(call tag,$*))" ] || { echo "  = $(call tag,$*) уже собран (FORCE=1 — пересобрать)"; exit 0; }
	docker build -f Dockerfile $(NO_CACHE_TEXT)				\
		--build-arg NGINX_VERSION=$*						\
		--build-arg BASE_IMAGE=$(call base_image,$*)		\
		--build-arg NJS_VERSION=$(NJS_VERSION)				\
		--tag $(call tag,$*)								\
		--progress plain $(BUILD_CONTEXT)


# тестирование модулей в собранном образе
.PHONY: test
test: test-$(VERSION)

.ONESHELL:
test-%: build-%
	@$(MAKE) check-$*

# прогон теста по готовому образу, без пересборки
.PHONY: check
check: check-$(VERSION)

.ONESHELL:
check-%:
	@docker run --rm --entrypoint /bin/bash -v $(TEST_DIR):/test:ro $(call tag,$*) /test/check.sh


.PHONY: clean
clean:
	rm -f "$(ARTIFACTS)/src/njs-$(NJS_VERSION).tar.gz"
	rm -rf "$(LINKED_ARTIFACTS)"


.PHONY: all
all: $(addprefix test-,$(VERSIONS))
