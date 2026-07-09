BASE_DIR      	?= $(abspath .)
ARTIFACTS     	?= $(BASE_DIR)/artifacts
BUILD_CONTEXT 	?= $(BASE_DIR)
TEST_DIR		?= $(BASE_DIR)/test


.DEFAULT_GOAL := help

# major-версии postgres, для которых собираем -ext образ (9.x без расширений)
VERSIONS		?= 10.23 11.22 12.22 13.23 14.23 15.18 16.14 17.10 18.4
VERSION      	?= $(lastword $(VERSIONS))
# выполнять ли сброс кэша: docker build --no-cache
NO_CACHE		?= 0

GH				:= https://github.com

# версии расширений (pgaudit — по одной на major, см. build-ext.sh)
PGAUDIT_VERS	:= 1.2.4 1.3.2 1.4.3 1.5.3 1.6.3 1.7.1 16.1 17.1 18.0
PGVECTOR_VER	:= 0.8.5
PGCRON_VER		:= 1.6.7
PGREPACK_VER	:= 1.5.3
PARTMAN_V4		:= 4.7.4
PARTMAN_V5		:= 5.4.3
TIMESCALE_VER	:= 2.28.2

IMAGE			:= dmp/postgres
NO_CACHE_TEXT	:= $(if $(filter 1,$(NO_CACHE)),--no-cache,)

# major из полной версии: 16.14 -> 16
major = $(firstword $(subst ., ,$(1)))

# shared_preload_libraries по major (timescaledb только с 15, должен быть первым)
ext_preload = $(shell m=$(call major,$(1)); \
	if [ "$$m" -ge 15 ]; then echo "timescaledb,pgaudit,pg_cron"; else echo "pgaudit,pg_cron"; fi)


.PHONY: help
help:
	@echo "make:"
	@echo "  make all        собрать -ext для всех версий"
	@echo "  make sources    скачать исходники расширений"
	@echo "  make build      собрать образ $(IMAGE):$(VERSION)-ext"
	@echo "  make test       проверить расширения в образе"
	@echo ""
	@echo "  Версии: $(VERSIONS)"
	@echo "  Набор: pgaudit, pg_cron, pg_repack, pg_partman, pgvector (>=13), timescaledb (>=15)."
	@echo "  Собирается поверх dmp/postgres:<ver>-dist, ставится в dmp/postgres:<ver>."
	@echo "  Другая версия: make build VERSION=16.14"
	@echo ""


.PHONY: debug
debug:
	$(info VERSIONS      = $(VERSIONS))
	$(info VERSION       = $(VERSION))
	$(info MAJOR         = $(call major,$(VERSION)))
	$(info IMAGE         = $(IMAGE):$(VERSION)-ext)
	@true


# Скачиваем исходники расширений
.PHONY: sources
sources:
	@set -e
	mkdir -p $(ARTIFACTS)/src
	cd $(ARTIFACTS)/src
	for v in $(PGAUDIT_VERS); do \
		[ -f "pgaudit-$$v.tar.gz" ] || curl -v -L -o "pgaudit-$$v.tar.gz" "$(GH)/pgaudit/pgaudit/archive/refs/tags/$$v.tar.gz"; done
	[ -f "pgvector-$(PGVECTOR_VER).tar.gz" ] || curl -v -L -o "pgvector-$(PGVECTOR_VER).tar.gz"   "$(GH)/pgvector/pgvector/archive/refs/tags/v$(PGVECTOR_VER).tar.gz"
	[ -f "pg_cron-$(PGCRON_VER).tar.gz" ] || curl -v -L -o "pg_cron-$(PGCRON_VER).tar.gz"      "$(GH)/citusdata/pg_cron/archive/refs/tags/v$(PGCRON_VER).tar.gz"
	[ -f "pg_repack-$(PGREPACK_VER).tar.gz" ] || curl -v -L -o "pg_repack-$(PGREPACK_VER).tar.gz"  "$(GH)/reorg/pg_repack/archive/refs/tags/ver_$(PGREPACK_VER).tar.gz"
	[ -f "pg_partman-$(PARTMAN_V4).tar.gz" ] || curl -v -L -o "pg_partman-$(PARTMAN_V4).tar.gz"   "$(GH)/pgpartman/pg_partman/archive/refs/tags/v$(PARTMAN_V4).tar.gz"
	[ -f "pg_partman-$(PARTMAN_V5).tar.gz" ] || curl -v -L -o "pg_partman-$(PARTMAN_V5).tar.gz"   "$(GH)/pgpartman/pg_partman/archive/refs/tags/v$(PARTMAN_V5).tar.gz"
	[ -f "timescaledb-$(TIMESCALE_VER).tar.gz" ] || curl -v -L -o "timescaledb-$(TIMESCALE_VER).tar.gz" "$(GH)/timescale/timescaledb/archive/refs/tags/$(TIMESCALE_VER).tar.gz"


# docker build - базовый postgres + расширения
.PHONY: build
build: sources build-$(VERSION)

.ONESHELL:
build-%:
	@current=$(*)
	set -e
	docker build -f Dockerfile $(NO_CACHE_TEXT)	\
		--build-arg PG_VERSION=$$current 		\
		--build-arg PG_MAJOR=$(call major,$*) 	\
		--build-arg PG_EXT_PRELOAD="$(call ext_preload,$*)" \
		--tag $(IMAGE):$$current-ext 			\
		--tag $(IMAGE):$(call major,$*)-ext 	\
		--progress plain $(BUILD_CONTEXT)


# тестирование расширений в образе
.PHONY: test
test: test-$(VERSION)

.ONESHELL:
test-%: build-%
	@current=$(*)
	docker run --rm --entrypoint /bin/bash -v $(TEST_DIR):/test:ro $(IMAGE):$$current-ext /test/extcheck.sh $(call major,$*)


.PHONY: all
all:
	@set -e
	$(MAKE) sources
	for v in $(VERSIONS); do $(MAKE) test-$$v; done
