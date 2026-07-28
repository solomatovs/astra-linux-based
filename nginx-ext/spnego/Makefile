BASE_DIR      	?= $(abspath .)
ARTIFACTS     	?= $(abspath $(BASE_DIR)/../../../ci-artifacts/nginx-ext/spnego)
BUILD_CONTEXT 	?= $(BASE_DIR)
# рабочая копия $(ARTIFACTS) внутри контекста сборки (хардлинки, см. artifacts-link)
LINKED_ARTIFACTS ?= $(BUILD_CONTEXT)/artifacts
TEST_DIR		?= $(BASE_DIR)/test


.DEFAULT_GOAL := help

# имя модуля (оно же суффикс тега образа)
EXT				:= spnego
# версии nginx, для которых собирается модуль
VERSIONS		?= 1.24.0 1.26.3 1.27.4 1.28.0
VERSION      	?= $(lastword $(VERSIONS))
# выполнять ли сброс кэша: docker build --no-cache
NO_CACHE		?= 0

IMAGE			:= dmp/nginx

# krb5 — зависимость модуля (gssapi)
KRB5_VERSION	?= 1.21.3
KRB5_SERIES		?= 1.21
KRB5_URL		?= https://kerberos.org/dist/krb5

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
	@echo "  make sources    скачать исходники зависимостей (krb5)"
	@echo "  make build      собрать образ $(call tag,$(VERSION))"
	@echo "  make test       проверить модуль в образе"
	@echo ""
	@echo "  Версии nginx: $(VERSIONS)"
	@echo "  Исходники модуля вендорятся в src/ (форк с поддержкой PAC-групп)."
	@echo "  Собирается в дереве $(IMAGE):<ver>-dist, ставится поверх $(call base_image,$(VERSION))."
	@echo "  Другая версия: make build VERSION=1.26.3"
	@echo "  Поверх других модулей: make build BASE_IMAGE=$(IMAGE):<ver>-njs TAG=$(IMAGE):<ver>-ext"
	@echo ""


.PHONY: debug
debug:
	$(info EXT           = $(EXT))
	$(info VERSIONS      = $(VERSIONS))
	$(info VERSION       = $(VERSION))
	$(info KRB5_VERSION  = $(KRB5_VERSION))
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


# Скачиваем исходники зависимостей (сам модуль лежит в src/)
.PHONY: sources
sources:
	@set -e
	mkdir -p $(ARTIFACTS)/src
	[ -f "$(ARTIFACTS)/src/krb5-$(KRB5_VERSION).tar.gz" ] || curl -v -L -o "$(ARTIFACTS)/src/krb5-$(KRB5_VERSION).tar.gz" \
		"$(KRB5_URL)/$(KRB5_SERIES)/krb5-$(KRB5_VERSION).tar.gz"


# docker build - базовый образ + модуль
.PHONY: build
build: build-$(VERSION)

.ONESHELL:
build-%: sources
	@set -e
	$(MAKE) --no-print-directory artifacts-link
	docker build -f Dockerfile $(NO_CACHE_TEXT)				\
		--build-arg NGINX_VERSION=$*						\
		--build-arg BASE_IMAGE=$(call base_image,$*)		\
		--build-arg KRB5_VERSION=$(KRB5_VERSION)			\
		--tag $(call tag,$*)								\
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
	rm -f "$(ARTIFACTS)/src/krb5-$(KRB5_VERSION).tar.gz"
	rm -rf "$(LINKED_ARTIFACTS)"


.PHONY: all
all:
	@set -e
	for v in $(VERSIONS); do $(MAKE) test-$$v; done
