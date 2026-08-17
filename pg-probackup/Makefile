BASE_DIR      	?= $(abspath .)
ARTIFACTS     	?= $(abspath $(BASE_DIR)/../../ci-artifacts/pg-probackup)
BUILD_CONTEXT 	?= $(BASE_DIR)
# рабочая копия $(ARTIFACTS) внутри контекста сборки (хардлинки, см. artifacts-link)
LINKED_ARTIFACTS ?= $(BUILD_CONTEXT)/artifacts
TEST_DIR		?= $(BASE_DIR)/test


.DEFAULT_GOAL := help

# major-версии postgres, с которыми линкуется релиз pg_probackup 2.5.16 (13-17).
# PG18 в 2.5.16 не собирается (undefined pg_detoast_datum_packed) — только master.
VERSIONS		?= 13.23 14.23 15.18 16.14 17.10
VERSION      	?= $(lastword $(VERSIONS))
# выполнять ли сброс кэша: docker build --no-cache
NO_CACHE		?= 0

# версия pg_probackup
PROBACKUP_VER	?= 2.5.16
# url исходников
GH				:= https://github.com
PG_URL			:= https://ftp.postgresql.org/pub/source

IMAGE			:= dmp/pg-probackup
NO_CACHE_TEXT	:= $(if $(filter 1,$(NO_CACHE)),--no-cache,)

# FORCE=1 — пересобрать, даже если образ уже есть
FORCE				?= 0

# уже собранные образы, снимок на момент запуска make
BUILT				:= $(if $(filter 1,$(FORCE) $(NO_CACHE)),,$(shell docker images --format '{{.Repository}}:{{.Tag}}' 2>/dev/null))
built = $(filter $(1),$(BUILT))
need_build = $(if $(call built,$(IMAGE):$(1)),,sources-$(1) artifacts-link)

# второй проход по зависимостям: только там уже известен $* (версия)
.SECONDEXPANSION:
# зависимости обновляются слева направо, artifacts-link должен отработать последним
.NOTPARALLEL:

.PHONY: build-all sources-all test-all
build-all:		$(addprefix build-,$(VERSIONS))
sources-all:		$(addprefix sources-,$(VERSIONS))
test-all:		$(addprefix test-,$(VERSIONS))


.PHONY: help
help:
	@echo "make:"
	@echo "  make all        собрать pg_probackup для всех версий"
	@echo "  make sources    скачать исходники pg_probackup и postgres версии $(VERSION)"
	@echo "  make build      собрать образ $(IMAGE):$(VERSION)"
	@echo "  make test       проверить: init каталога + FULL --stream бэкап"
	@echo ""
	@echo "  pg_probackup $(PROBACKUP_VER), postgres $(VERSIONS)"
	@echo "  Собирается против исходников postgres поверх dmp/postgres:<ver>-dist."
	@echo ""


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


# Скачиваем исходники pg_probackup и postgres
.PHONY: sources
sources: sources-$(VERSION)

.ONESHELL:
sources-%:
	@current=$(*)
	set -e
	mkdir -p $(ARTIFACTS)/src
	[ -f "$(ARTIFACTS)/src/pg_probackup-$(PROBACKUP_VER).tar.gz" ] || curl -v -L -o "$(ARTIFACTS)/src/pg_probackup-$(PROBACKUP_VER).tar.gz" \
		"$(GH)/postgrespro/pg_probackup/archive/refs/tags/$(PROBACKUP_VER).tar.gz"
	[ -f "$(ARTIFACTS)/src/postgresql-$$current.tar.bz2" ] || curl -v -L -o "$(ARTIFACTS)/src/postgresql-$$current.tar.bz2" \
		"$(PG_URL)/v$$current/postgresql-$$current.tar.bz2"


# docker build - образ с pg_probackup
.PHONY: build
build: build-$(VERSION)

.ONESHELL:
build-%: $$(call need_build,$$*)
	@current=$(*)
	set -e
	[ -z "$(call built,$(IMAGE):$*)" ] || { echo "  = $(IMAGE):$* уже собран (FORCE=1 — пересобрать)"; exit 0; }
	docker build -f Dockerfile $(NO_CACHE_TEXT)	\
		--build-arg PG_VERSION=$$current 		\
		--build-arg PROBACKUP_VERSION=$(PROBACKUP_VER) \
		--tag $(IMAGE):$$current 				\
		--progress plain $(BUILD_CONTEXT)


# тестирование
.PHONY: test
test: test-$(VERSION)

.ONESHELL:
test-%: build-%
	@current=$(*)
	docker run --rm --entrypoint /bin/bash -v $(TEST_DIR):/test:ro $(IMAGE):$$current /test/pbcheck.sh


.PHONY: all
all: $(addprefix test-,$(VERSIONS))
