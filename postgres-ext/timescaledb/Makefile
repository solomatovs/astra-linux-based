BASE_DIR      	?= $(abspath .)
ARTIFACTS     	?= $(abspath $(BASE_DIR)/../../../ci-artifacts/postgres-ext/timescaledb)
BUILD_CONTEXT 	?= $(BASE_DIR)
# рабочая копия $(ARTIFACTS) внутри контекста сборки (хардлинки, см. artifacts-link)
LINKED_ARTIFACTS ?= $(BUILD_CONTEXT)/artifacts
TEST_DIR		?= $(BASE_DIR)/test


.DEFAULT_GOAL := help

# имя расширения, оно же суффикс тега образа
EXT				:= timescaledb
# версии postgres, для которых собирается расширение (timescaledb 2.28 требует >= 15)
VERSIONS		?= 15.18 16.14 17.10 18.4
# целевая версия сборки, по умолчанию последняя
VERSION      	?= $(lastword $(VERSIONS))
# версия расширения
EXT_VERSION		?= 2.28.2
# выполнять ли сброс кэша: docker build --no-cache
NO_CACHE		?= 0

GH				:= https://github.com
IMAGE			:= dmp/postgres

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

.PHONY: build-all test-all
build-all:		$(addprefix build-,$(VERSIONS))
test-all:		$(addprefix test-,$(VERSIONS))

# major из полной версии: 16.14 -> 16
major = $(firstword $(subst ., ,$(1)))

# базовый образ и тег результата, при необходимости переопределяются из командной строки
BASE_IMAGE		?=
TAG				?=
base_image = $(if $(BASE_IMAGE),$(BASE_IMAGE),$(IMAGE):$(1))
tag        = $(if $(TAG),$(TAG),$(IMAGE):$(1)-$(EXT))


.PHONY: help
help:
	@echo "make ($(EXT) $(EXT_VERSION)):"
	@echo "  make all        собрать для всех поддерживаемых версий postgres"
	@echo "  make sources    скачать исходники $(EXT)"
	@echo "  make build      собрать образ $(call tag,$(VERSION))"
	@echo "  make test       проверить расширение в образе"
	@echo ""
	@echo "  Версии postgres: $(VERSIONS) (требуется >= 15)"
	@echo "  Собирается cmake (bootstrap -DAPACHE_ONLY=ON), только apache-лицензия."
	@echo "  Образ несёт свой entrypoint: preload и telemetry_level прописываются при initdb."
	@echo "  Другая версия: make build VERSION=16.14"
	@echo ""


.PHONY: debug
debug:
	$(info EXT           = $(EXT))
	$(info VERSIONS      = $(VERSIONS))
	$(info VERSION       = $(VERSION))
	$(info MAJOR         = $(call major,$(VERSION)))
	$(info EXT_VERSION   = $(EXT_VERSION))
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


# Скачиваем исходники расширения
.PHONY: sources
sources:
	@set -e
	mkdir -p $(ARTIFACTS)/src
	[ -f "$(ARTIFACTS)/src/$(EXT)-$(EXT_VERSION).tar.gz" ] || curl -v -L -o "$(ARTIFACTS)/src/$(EXT)-$(EXT_VERSION).tar.gz" \
		"$(GH)/timescale/timescaledb/archive/refs/tags/$(EXT_VERSION).tar.gz"


# docker build - базовый образ + расширение
.PHONY: build
build: build-$(VERSION)

.ONESHELL:
build-%: $$(call need_build,$$*)
	@set -e
	[ -z "$(call built,$(call tag,$*))" ] || { echo "  = $(call tag,$*) уже собран (FORCE=1 — пересобрать)"; exit 0; }
	docker build -f Dockerfile $(NO_CACHE_TEXT)				\
		--build-arg PG_VERSION=$*							\
		--build-arg BASE_IMAGE=$(call base_image,$*)		\
		--build-arg EXT_VERSION=$(EXT_VERSION)				\
		--tag $(call tag,$*)								\
		--progress plain $(BUILD_CONTEXT)


# тестирование расширения в собранном образе
.PHONY: test
test: test-$(VERSION)

.ONESHELL:
test-%: build-%
	@docker run --rm --entrypoint /bin/bash -v $(TEST_DIR):/test:ro $(call tag,$*) /test/check.sh


.PHONY: all
all: $(addprefix test-,$(VERSIONS))
