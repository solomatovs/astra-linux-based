BASE_DIR      	?= $(abspath .)
ARTIFACTS     	?= $(BASE_DIR)/artifacts
BUILD_CONTEXT 	?= $(BASE_DIR)
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


# helper: флаги ./configure по версии
# openssl c 9.5, icu c 10, uuid c 9.4; perl/tcl всегда, python (python3.7) c 10
pg_extra = $(shell v='$(1)'; maj=$${v%%.*}; min=$$(echo "$$v" | cut -d. -f2);	\
	e="--with-libxml --with-readline --with-zlib --with-perl --with-tcl";		\
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
		--build-arg PG_VERSION=$$current 						\
		--build-arg PG_EXTRA_CONF="$(call pg_extra,$*)" 		\
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
		--build-arg PG_VERSION=$$current 						\
		--build-arg PG_EXTRA_CONF="$(call pg_extra,$*)" 		\
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

.ONESHELL:
clean-%:
	@current=$(*)
	set -e
	rm -f "$(ARTIFACTS)/src/postgresql-$$current.tar.bz2"


.PHONY: all
all:
	@set -e
	for v in $(VERSIONS); do $(MAKE) test-$$v; done
