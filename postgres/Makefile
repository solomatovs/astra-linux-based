BASE_DIR      	?= $(abspath .)
ARTIFACTS     	?= $(abspath $(BASE_DIR)/../../ci-artifacts/postgres)
BUILD_CONTEXT 	?= $(BASE_DIR)
# рабочая копия $(ARTIFACTS) внутри контекста сборки (хардлинки, см. artifacts-link)
LINKED_ARTIFACTS ?= $(BUILD_CONTEXT)/artifacts
TEST_DIR		?= $(BASE_DIR)/test


.DEFAULT_GOAL := help

# список версий, которые поддерживает сборщик
# major-версии postgres независимы, собираются отдельными целями
VERSIONS			?= 9.0.23 9.1.24 9.2.24 9.3.25 9.4.26 9.5.25 9.6.24 \
					   10.23 11.22 12.22 13.23 14.23 15.18 16.14 17.10 18.4
# целевая версия сборки. по умолчанию берется последняя версия
VERSION      		?= $(lastword $(VERSIONS))
# базовый url адрес для скачивания исходников собираемых в Dockerfile
BASE_URL			?= https://ftp.postgresql.org/pub/source
# MIT krb5 (GSSAPI/Kerberos для --with-gssapi), тот же, что в nginx/samba-ad
KRB5_VERSION		?= 1.21.3
KRB5_SERIES			?= 1.21
KRB5_URL			?= https://kerberos.org/dist/krb5
# выполнять ли сброс кэша: docker build --no-cache
NO_CACHE			?= 0

# apt-deps (dev)
APT_DEPS			?= pkg-config bison flex file bzip2			\
					libssl-dev libreadline-dev libncurses5-dev	\
					zlib1g-dev libxml2-dev libxslt1-dev			\
					libicu-dev uuid-dev							\
					libperl-dev tcl-dev python3.7-dev
# apt-deps (runtime)
RUNTIME_APT_DEPS	?= libssl1.1 libreadline7 libncurses5 zlib1g\
					libxml2 libxslt1.1 libicu63 libuuid1		\
					perl libtcl8.6 libpython3.7					\
					ca-certificates tzdata

GLIBC_IMAGE			?= dmp/glibc:2.28
GCC_IMAGE			?= dmp/gcc:8.5.0-dist

# зависимости для сборки разных версии
# версия           runtime_image      build_image
DEPS_9.0.23		:= $(GLIBC_IMAGE)    $(GCC_IMAGE)
DEPS_9.1.24		:= $(GLIBC_IMAGE)    $(GCC_IMAGE)
DEPS_9.2.24		:= $(GLIBC_IMAGE)    $(GCC_IMAGE)
DEPS_9.3.25		:= $(GLIBC_IMAGE)    $(GCC_IMAGE)
DEPS_9.4.26		:= $(GLIBC_IMAGE)    $(GCC_IMAGE)
DEPS_9.5.25		:= $(GLIBC_IMAGE)    $(GCC_IMAGE)
DEPS_9.6.24		:= $(GLIBC_IMAGE)    $(GCC_IMAGE)
DEPS_10.23		:= $(GLIBC_IMAGE)    $(GCC_IMAGE)
DEPS_11.22		:= $(GLIBC_IMAGE)    $(GCC_IMAGE)
DEPS_12.22		:= $(GLIBC_IMAGE)    $(GCC_IMAGE)
DEPS_13.23		:= $(GLIBC_IMAGE)    $(GCC_IMAGE)
DEPS_14.23		:= $(GLIBC_IMAGE)    $(GCC_IMAGE)
DEPS_15.18		:= $(GLIBC_IMAGE)    $(GCC_IMAGE)
DEPS_16.14		:= $(GLIBC_IMAGE)    $(GCC_IMAGE)
DEPS_17.10		:= $(GLIBC_IMAGE)    $(GCC_IMAGE)
DEPS_18.4		:= $(GLIBC_IMAGE)    $(GCC_IMAGE)

# базовое название для образа
IMAGE			:= dmp/postgres
NO_CACHE_TEXT	:= $(if $(filter 1,$(NO_CACHE)),--no-cache,)

# FORCE=1 — пересобрать, даже если образ уже есть
FORCE				?= 0

# уже собранные образы, снимок на момент запуска make
BUILT				:= $(if $(filter 1,$(FORCE) $(NO_CACHE)),,$(shell docker images --format '{{.Repository}}:{{.Tag}}' 2>/dev/null))
built = $(filter $(1),$(BUILT))
need_dist = $(if $(call built,$(IMAGE):$(1)-dist),,sources-$(1) artifacts-link)
need_build = $(if $(call built,$(IMAGE):$(1)),,dist-$(1) artifacts-link)

# второй проход по зависимостям: только там уже известен $* (версия)
.SECONDEXPANSION:
# зависимости обновляются слева направо, artifacts-link должен отработать последним
.NOTPARALLEL:

.PHONY: build-all clean-all dist-all sources-all test-all
build-all:		$(addprefix build-,$(VERSIONS))
clean-all:		$(addprefix clean-,$(VERSIONS))
dist-all:		$(addprefix dist-,$(VERSIONS))
sources-all:		$(addprefix sources-,$(VERSIONS))
test-all:		$(addprefix test-,$(VERSIONS))


# helper: флаги ./configure по версии
# openssl c 9.5, icu c 10, uuid c 9.4; perl/tcl всегда, python (python3.7) c 10
pg_extra = $(shell v='$(1)'; maj=$${v%%.*}; min=$$(echo "$$v" | cut -d. -f2);	\
	e="--with-libxml --with-readline --with-zlib --with-perl --with-tcl --with-gssapi";	\
	if [ "$$maj" -ge 10 ] || { [ "$$maj" -eq 9 ] && [ "$$min" -ge 5 ]; }; then	\
		e="$$e --with-openssl"; fi;												\
	if [ "$$maj" -ge 10 ]; then e="$$e --with-icu --with-python"; fi;			\
	if [ "$$maj" -ge 10 ] || { [ "$$maj" -eq 9 ] && [ "$$min" -ge 4 ]; }; then	\
		e="$$e --with-uuid=e2fs"; fi;											\
	echo "$$e")


.PHONY: help
help:
	@echo "make:"
	@echo "  make all        собрать всё (перебор VERSIONS)"
	@echo "  make sources    скачать исходники postgres версии $(VERSION)"
	@echo "  make dist       собрать -dist образ версии $(VERSION)"
	@echo "  make build      собрать образ $(IMAGE):$(VERSION)"
	@echo "  make test       проверить образ: initdb, старт сервера, psql, contrib"
	@echo ""
	@echo "  Версии: $(VERSIONS)"
	@echo "  Каждая версия собирается тулчейном $(GCC_IMAGE), рантайм — $(GLIBC_IMAGE)."
	@echo "  Флаги ./configure подбираются по версии (см. pg_extra)."
	@echo "  Другая версия: make build VERSION=16.14"
	@echo ""


.PHONY: debug
debug:
	$(info BASE_DIR         = $(BASE_DIR))
	$(info ARTIFACTS        = $(ARTIFACTS))
	$(info BUILD_CONTEXT    = $(BUILD_CONTEXT))
	$(info LINKED_ARTIFACTS = $(LINKED_ARTIFACTS))
	$(info TEST_DIR         = $(TEST_DIR))
	$(info VERSIONS         = $(VERSIONS))
	$(info VERSION          = $(VERSION))
	$(info BASE_URL         = $(BASE_URL))
	$(info NO_CACHE         = $(NO_CACHE))
	$(info NO_CACHE_TEXT    = $(NO_CACHE_TEXT))
	$(info APT_DEPS         = $(APT_DEPS))
	$(info RUNTIME_APT_DEPS = $(RUNTIME_APT_DEPS))
	$(info IMAGE            = $(IMAGE):$(VERSION))
	$(info DEPS($(VERSION)) = $(DEPS_$(VERSION)))
	$(info CONF($(VERSION)) = $(call pg_extra,$(VERSION)))
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


# Скачиваем исходники postgres
.PHONY: sources
sources: sources-$(VERSION)

.ONESHELL:
sources-%:
	@current=$(*)
	set -e
	mkdir -p $(ARTIFACTS)/src
	[ -f "$(ARTIFACTS)/src/postgresql-$$current.tar.bz2" ] || curl -v -L -o "$(ARTIFACTS)/src/postgresql-$$current.tar.bz2" \
		"$(BASE_URL)/v$$current/postgresql-$$current.tar.bz2"
	[ -f "$(ARTIFACTS)/src/krb5-$(KRB5_VERSION).tar.gz" ] || curl -v -L -o "$(ARTIFACTS)/src/krb5-$(KRB5_VERSION).tar.gz" \
		"$(KRB5_URL)/$(KRB5_SERIES)/krb5-$(KRB5_VERSION).tar.gz"


# docker build --target dist - дистрибутив с скомпилированным сервером (без установки в систему)
.PHONY: dist
dist: dist-$(VERSION)

.ONESHELL:
dist-%: $$(call need_dist,$$*)
	@current=$(*)
	set -e
	set -- $(DEPS_$*)
	runtime=$$1; build=$$2
	short=$${current%.*}
	[ -z "$(call built,$(IMAGE):$*-dist)" ] || { echo "  = $(IMAGE):$*-dist уже собран (FORCE=1 — пересобрать)"; exit 0; }
	docker build -f Dockerfile $(NO_CACHE_TEXT) --target dist	\
		--build-arg RUNTIME_IMAGE=$$runtime 					\
		--build-arg BUILD_IMAGE=$$build 						\
		--build-arg PG_VERSION=$$current 						\
		--build-arg PG_EXTRA_CONF="$(call pg_extra,$*)" 		\
		--build-arg KRB5_VERSION=$(KRB5_VERSION) 				\
		--build-arg APT_DEPS="$(APT_DEPS)" 						\
		--tag $(IMAGE):$$current-dist 							\
		--tag $(IMAGE):$$short-dist 							\
		--progress plain $(BUILD_CONTEXT)


# docker build - истоговый образ с установленным сервером (установка из дистрибутива)
.PHONY: build
build: build-$(VERSION)

.ONESHELL:
build-%: $$(call need_build,$$*)
	@current=$(*)
	set -e
	set -- $(DEPS_$*)
	runtime=$$1; build=$$2
	short=$${current%.*}
	[ -z "$(call built,$(IMAGE):$*)" ] || { echo "  = $(IMAGE):$* уже собран (FORCE=1 — пересобрать)"; exit 0; }
	docker build -f Dockerfile $(NO_CACHE_TEXT) 				\
		--build-arg RUNTIME_IMAGE=$$runtime 					\
		--build-arg BUILD_IMAGE=$$build 						\
		--build-arg PG_VERSION=$$current 						\
		--build-arg PG_EXTRA_CONF="$(call pg_extra,$*)" 		\
		--build-arg KRB5_VERSION=$(KRB5_VERSION) 				\
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
	docker run --rm --entrypoint /bin/bash -v $(TEST_DIR):/test:ro $(IMAGE):$$current /test/pgcheck.sh


.PHONY: clean
clean: clean-$(VERSION)
	rm -rf "$(LINKED_ARTIFACTS)"

.ONESHELL:
clean-%:
	@current=$(*)
	set -e
	rm -f "$(ARTIFACTS)/src/postgresql-$$current.tar.bz2"


.PHONY: all
all: $(addprefix test-,$(VERSIONS))
