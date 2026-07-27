BASE_DIR      	?= $(abspath .)
ARTIFACTS     	?= $(BASE_DIR)/artifacts
BUILD_CONTEXT 	?= $(BASE_DIR)
TEST_DIR		?= $(BASE_DIR)/test


.DEFAULT_GOAL := help

# имя модуля (оно же суффикс тега образа)
EXT				:= auth-ldap
# версии nginx, для которых собирается модуль
VERSIONS		?= 1.24.0 1.26.3 1.27.4 1.28.0
VERSION      	?= $(lastword $(VERSIONS))
# выполнять ли сброс кэша: docker build --no-cache
NO_CACHE		?= 0

IMAGE			:= dmp/nginx

# исходники модуля (у проекта нет релизов — берём ветку)
MODULE_REF		?= master
MODULE_URL		?= https://github.com/kvspb/nginx-auth-ldap/archive/refs/heads
# OpenLDAP client — зависимость модуля
OPENLDAP_VERSION ?= 2.6.9
OPENLDAP_URL	?= https://www.openldap.org/software/download/OpenLDAP/openldap-release

NO_CACHE_TEXT	:= $(if $(filter 1,$(NO_CACHE)),--no-cache,)

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
	@echo "  make sources    скачать исходники модуля и зависимостей (openldap)"
	@echo "  make build      собрать образ $(call tag,$(VERSION))"
	@echo "  make test       проверить модуль в образе"
	@echo ""
	@echo "  Версии nginx: $(VERSIONS)"
	@echo "  Собирается в дереве $(IMAGE):<ver>-dist, ставится поверх $(call base_image,$(VERSION))."
	@echo "  Другая версия: make build VERSION=1.26.3"
	@echo "  Поверх других модулей: make build BASE_IMAGE=$(IMAGE):<ver>-njs TAG=$(IMAGE):<ver>-ext"
	@echo ""


.PHONY: debug
debug:
	$(info EXT              = $(EXT))
	$(info VERSIONS         = $(VERSIONS))
	$(info VERSION          = $(VERSION))
	$(info MODULE_REF       = $(MODULE_REF))
	$(info OPENLDAP_VERSION = $(OPENLDAP_VERSION))
	$(info BASE_IMAGE       = $(call base_image,$(VERSION)))
	$(info TAG              = $(call tag,$(VERSION)))
	@true



# Скачиваем исходники модуля и его зависимостей
.PHONY: sources
sources:
	@set -e
	mkdir -p $(ARTIFACTS)/src
	[ -f "$(ARTIFACTS)/src/nginx-auth-ldap-$(MODULE_REF).tar.gz" ] || curl -v -L -o "$(ARTIFACTS)/src/nginx-auth-ldap-$(MODULE_REF).tar.gz" \
		"$(MODULE_URL)/$(MODULE_REF).tar.gz"
	[ -f "$(ARTIFACTS)/src/openldap-$(OPENLDAP_VERSION).tgz" ] || curl -v -L -o "$(ARTIFACTS)/src/openldap-$(OPENLDAP_VERSION).tgz" \
		"$(OPENLDAP_URL)/openldap-$(OPENLDAP_VERSION).tgz"


# docker build - базовый образ + модуль
.PHONY: build
build: build-$(VERSION)

.ONESHELL:
build-%: sources
	@set -e
	docker build -f Dockerfile $(NO_CACHE_TEXT)					\
		--build-arg NGINX_VERSION=$*							\
		--build-arg BASE_IMAGE=$(call base_image,$*)			\
		--build-arg OPENLDAP_VERSION=$(OPENLDAP_VERSION)		\
		--build-arg MODULE_REF=$(MODULE_REF)					\
		--tag $(call tag,$*)									\
		--progress plain $(BUILD_CONTEXT)


# тестирование модуля в собранном образе
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
	rm -f "$(ARTIFACTS)/src/nginx-auth-ldap-$(MODULE_REF).tar.gz" \
		"$(ARTIFACTS)/src/openldap-$(OPENLDAP_VERSION).tgz"


.PHONY: all
all:
	@set -e
	for v in $(VERSIONS); do $(MAKE) test-$$v; done
