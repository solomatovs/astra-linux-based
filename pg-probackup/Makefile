BASE_DIR      	?= $(abspath .)
ARTIFACTS     	?= $(BASE_DIR)/artifacts
BUILD_CONTEXT 	?= $(BASE_DIR)
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


# Скачиваем исходники pg_probackup и postgres
.PHONY: sources
sources: sources-$(VERSION)

.ONESHELL:
sources-%:
	@current=$(*)
	set -e
	mkdir -p $(ARTIFACTS)/src
	curl -v -L --skip-existing -o "$(ARTIFACTS)/src/pg_probackup-$(PROBACKUP_VER).tar.gz" \
		"$(GH)/postgrespro/pg_probackup/archive/refs/tags/$(PROBACKUP_VER).tar.gz"
	curl -v -L --skip-existing -o "$(ARTIFACTS)/src/postgresql-$$current.tar.bz2" \
		"$(PG_URL)/v$$current/postgresql-$$current.tar.bz2"


# docker build - образ с pg_probackup
.PHONY: build
build: build-$(VERSION)

.ONESHELL:
build-%: sources-%
	@current=$(*)
	set -e
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
all:
	@set -e
	for v in $(VERSIONS); do $(MAKE) test-$$v; done
