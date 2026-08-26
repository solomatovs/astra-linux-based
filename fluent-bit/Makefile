BASE_DIR      	?= $(abspath .)
ARTIFACTS     	?= $(abspath $(BASE_DIR)/../../ci-artifacts/fluent-bit)
BUILD_CONTEXT 	?= $(BASE_DIR)
# рабочая копия $(ARTIFACTS) внутри контекста сборки (хардлинки, см. artifacts-link)
LINKED_ARTIFACTS ?= $(BUILD_CONTEXT)/artifacts
TEST_DIR		?= $(BASE_DIR)/test


.DEFAULT_GOAL := help

# список версий, которые поддерживает сборщик
# версии fluent-bit независимы, но собираются одна за другой, строго по цепочке
VERSION_CHAIN		?= 3.2.10 4.0.14 4.2.6 5.0.9
# целевая версия сборки. по умолчанию берется последняя версия
VERSION      		?= $(lastword $(VERSION_CHAIN))
# базовые url адреса (конкретные версии подставляются в цели sources)
# fluent-bit (github archive: .../archive/refs/tags/v<версия>.tar.gz)
BASE_URL			?= https://github.com/fluent/fluent-bit/archive/refs/tags
# openssl (releases/download/openssl-<версия>/openssl-<версия>.tar.gz)
OPENSSL_URL			?= https://github.com/openssl/openssl/releases/download
# libyaml (releases/download/<версия>/yaml-<версия>.tar.gz)
LIBYAML_URL			?= https://github.com/yaml/libyaml/releases/download
# postgres (v<версия>/postgresql-<версия>.tar.gz) — из него берётся только libpq
# для выходного плагина out_pgsql
POSTGRES_URL		?= https://ftp.postgresql.org/pub/source
# docker build --no-cache
NO_CACHE			?= 0
# apt-deps (build): flex/bison/m4 нужны генератору парсеров (и configure у postgres),
# pkg-config — cmake-поиску, libsystemd-dev — заголовки sd-journal для in_systemd,
# libkrb5-dev — заголовки GSSAPI для libpq. Только заголовки: сами .so есть в
# рантайм-образе, libc6-dev ни один пакет не тянет (проверено apt-get install -s)
APT_DEPS			?= flex bison m4 pkg-config libsystemd-dev libkrb5-dev

GLIBC_IMAGE			?= dmp/glibc:2.28
GCC_IMAGE			?= dmp/gcc:14.2.0-dist
CMAKE_IMAGE			?= dmp/cmake:3.27.9-dist

# зависимости для сборки разных версии
# openssl/libyaml/libpq собираются из исходников и линкуются СТАТИЧЕСКИ
# postgres: версия берётся ради клиентской библиотеки libpq (out_pgsql). Ставьте не
# ниже самого нового сервера, к которому будете подключаться: libpq 9.6 не умеет
# scram-sha-256, а это дефолт аутентификации с postgres 10
# avro: кодек берётся из вендоренных lib/avro и lib/jansson. в 4.0.14 включить
# нельзя — jansson и nghttp2 объявляют одну и ту же цель cmake "check"
# версия      runtime_image    build_image     cmake_image      openssl   libyaml  postgres  avro
DEPS_3.2.10	:= $(GLIBC_IMAGE)   $(GCC_IMAGE)    $(CMAKE_IMAGE)   3.0.16    0.2.5    18.4      On
DEPS_4.0.14	:= $(GLIBC_IMAGE)   $(GCC_IMAGE)    $(CMAKE_IMAGE)   3.0.16    0.2.5    18.4      Off
DEPS_4.2.6	:= $(GLIBC_IMAGE)   $(GCC_IMAGE)    $(CMAKE_IMAGE)   3.0.16    0.2.5    18.4      On
DEPS_5.0.9	:= $(GLIBC_IMAGE)   $(GCC_IMAGE)    $(CMAKE_IMAGE)   3.0.16    0.2.5    18.4      On

# базовое название для образа
IMAGE			:= dmp/fluent-bit
FIRST_VERSION	:= $(firstword $(VERSION_CHAIN))
NO_CACHE_TEXT	:= $(if $(filter 1,$(NO_CACHE)),--no-cache,)


# FORCE=1 — пересобрать, даже если образ уже есть
FORCE				?= 0

# уже собранные образы, снимок на момент запуска make
BUILT				:= $(if $(filter 1,$(FORCE) $(NO_CACHE)),,$(shell docker images --format '{{.Repository}}:{{.Tag}}' 2>/dev/null))
built = $(filter $(1),$(BUILT))

# зависимости версии берутся из DEPS_<версия>: ссылка на образ этого же проекта
# означает, что сначала надо собрать то звено
deps_of = $(patsubst $(IMAGE):%-dist,dist-%,$(filter $(IMAGE):%-dist,$(DEPS_$(1)))) \
          $(patsubst $(IMAGE):%,build-%,$(filter-out %-dist,$(filter $(IMAGE):%,$(DEPS_$(1)))))

# что собрать перед версией; если образ уже есть — ничего
need_dist  = $(if $(call built,$(IMAGE):$(1)-dist),,sources-$(1) $(call deps_of,$(1)) artifacts-link)
need_build = $(if $(call built,$(IMAGE):$(1)),,dist-$(1) $(call deps_of,$(1)) artifacts-link)

# второй проход по списку зависимостей: только там уже известен $* (версия),
# поэтому $$(call need_dist,$$*) можно вычислить
.SECONDEXPANSION:
# зависимости обновляются слева направо, artifacts-link должен отработать последним
.NOTPARALLEL:

.PHONY: sources-all dist-all build-all test-all clean-all
sources-all:		$(addprefix sources-,$(VERSION_CHAIN))
dist-all:		$(addprefix dist-,$(VERSION_CHAIN))
build-all:		$(addprefix build-,$(VERSION_CHAIN))
test-all:		$(addprefix test-,$(VERSION_CHAIN))
clean-all:		$(addprefix clean-,$(VERSION_CHAIN))


.PHONY: help
help:
	@echo "make:"
	@echo "  make all        собрать всё"
	@echo "  make sources    скачать исходники (fluent-bit/openssl/libyaml/postgres) для $(VERSION)"
	@echo "  make dist       собрать -dist образ $(VERSION) (звенья цепочки — если нужны)"
	@echo "  make build      собрать образ $(IMAGE)"
	@echo "  make test       проверить образ: fluent-bit --version, список плагинов, smoke-пайплайн"
	@echo "  make stand      поднять стенд postgres+clickhouse+fluent-bit и проверить свои плагины"
	@echo "  make stand-down убрать стенд"
	@echo ""
	@echo "  Цепочка: $(VERSION_CHAIN)"
	@echo "  Каждая версия собирается тулчейном dmp/gcc, cmake берётся из dmp/cmake, рантайм — чистый dmp/glibc."
	@echo "  Максимум модулей: FLB_ALL + всё, что апстрим держит выключенным по умолчанию —"
	@echo "  out_pgsql (свой статический libpq), in_systemd (journald), avro-кодек,"
	@echo "  in_unix_socket, in_event_test, out_retry."
	@echo "  Свои плагины из plugins/ собираются рядом как .so и перечисляются в"
	@echo "  /usr/local/etc/fluent-bit/plugins.conf: postgres (libpq, Kerberos) и"
	@echo "  clickhouse (SQL по HTTP) есть и как вход (записи либо метрики, Mode),"
	@echo "  и как выход (произвольный SQL над пачкой); in_pg2ch — насос PG COPY ->"
	@echo "  CH INSERT TabSeparated по расписанию."
	@echo "  Запросы и примеры едут в образе: /usr/local/share/fluent-bit/{queries,examples}."
	@echo "  Не собираются (нужен чужой тулчейн, недоступный на этой базе):"
	@echo "    in_ebpf        — clang с целью bpf, bpftool и libbpf/libelf (в astra 2.12 их нет)"
	@echo "    filter_throttle_size — не собирается с текущим API flb_hash_table (баг апстрима)"
	@echo "    filter_tensorflow — libtensorflow"
	@echo "    FLB_ARROW      — Apache Arrow GLib"
	@echo "    FLB_WAMRC      — AOT-компилятор WASM, требует сборки LLVM внутри WAMR"
	@echo "    FLB_ZIG        — компилятор zig ровно версии 0.14.0"
	@echo "    winlog/winevtlog/winstat — только Windows"
	@echo "  OpenSSL/libyaml/libpq собираются из исходников и линкуются СТАТИЧЕСКИ."
	@echo "  Единственная внешняя .so — libsystemd.so.0 из базового образа (ради in_systemd)."
	@echo "  libstdc++/libgcc линкуются статически — рантайму нужен только glibc."
	@echo "  Каждый образ тегируется полной и короткой версией (напр. 5.0.9 и 5.0)."
	@echo "  Версии зависимостей — в DEPS_<версия>."
	@echo "  Другая версия: make all VERSION=4.2.6"
	@echo "  Только одна версия (без цепочки): make build VERSION=5.0.9 VERSION_CHAIN=5.0.9"
	@echo ""
	@echo "  make <стадия>-all  то же для всех версий из VERSION_CHAIN"
	@echo "  FORCE=1            пересобрать, даже если образ уже есть"


.PHONY: debug
debug:
	$(info BASE_DIR       = $(BASE_DIR))
	$(info ARTIFACTS      = $(ARTIFACTS))
	$(info BUILD_CONTEXT  = $(BUILD_CONTEXT))
	$(info LINKED_ARTIFACTS = $(LINKED_ARTIFACTS))
	$(info TEST_DIR       = $(TEST_DIR))
	$(info VERSION_CHAIN  = $(VERSION_CHAIN))
	$(info VERSION        = $(VERSION))
	$(info FIRST_VERSION  = $(FIRST_VERSION))
	$(info BASE_URL       = $(BASE_URL))
	$(info OPENSSL_URL    = $(OPENSSL_URL))
	$(info LIBYAML_URL    = $(LIBYAML_URL))
	$(info POSTGRES_URL   = $(POSTGRES_URL))
	$(info NO_CACHE       = $(NO_CACHE))
	$(info NO_CACHE_TEXT  = $(NO_CACHE_TEXT))
	$(info APT_DEPS       = $(APT_DEPS))
	$(info IMAGE          = $(IMAGE):$(VERSION))
	$(info DEPS($(VERSION)) = $(DEPS_$(VERSION)))
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


# Скачиваем зависимости для сборки дистрибутива
.PHONY: sources
sources: sources-$(VERSION)

.ONESHELL:
sources-%:
	@current=$(*)
	set -e
	set -- $(DEPS_$*)
	openssl=$$4; libyaml=$$5; postgres=$$6
	mkdir -p $(ARTIFACTS)/src
	# полные url собираются из базового url и версии (версии — в DEPS_<версия>)
	[ -f "$(ARTIFACTS)/src/fluent-bit-$$current.tar.gz" ] || curl -v -L -o "$(ARTIFACTS)/src/fluent-bit-$$current.tar.gz" "$(BASE_URL)/v$$current.tar.gz"
	[ -f "$(ARTIFACTS)/src/openssl-$$openssl.tar.gz" ] || curl -v -L -o "$(ARTIFACTS)/src/openssl-$$openssl.tar.gz" "$(OPENSSL_URL)/openssl-$$openssl/openssl-$$openssl.tar.gz"
	[ -f "$(ARTIFACTS)/src/yaml-$$libyaml.tar.gz" ] || curl -v -L -o "$(ARTIFACTS)/src/yaml-$$libyaml.tar.gz" "$(LIBYAML_URL)/$$libyaml/yaml-$$libyaml.tar.gz"
	# .tar.gz, а не .tar.bz2 как у соседнего проекта postgres: bzip2 в сборочном образе нет
	[ -f "$(ARTIFACTS)/src/postgresql-$$postgres.tar.gz" ] || curl -v -L -o "$(ARTIFACTS)/src/postgresql-$$postgres.tar.gz" "$(POSTGRES_URL)/v$$postgres/postgresql-$$postgres.tar.gz"


# docker build --target dist - дистрибутив со скомпилированными исходниками (без установки в систему)
.PHONY: dist
dist: dist-$(VERSION)

.ONESHELL:
dist-%: $$(call need_dist,$$*)
	@current=$(*)
	set -e
	set -- $(DEPS_$*)
	runtime=$$1; build=$$2; cmake=$$3; openssl=$$4; libyaml=$$5; postgres=$$6; avro=$$7
	short=$${current%.*}
	[ -z "$(call built,$(IMAGE):$*-dist)" ] || { echo "  = $(IMAGE):$*-dist уже собран (FORCE=1 — пересобрать)"; exit 0; }
	docker build -f Dockerfile $(NO_CACHE_TEXT) --target dist \
		--build-arg RUNTIME_IMAGE=$$runtime 		\
		--build-arg BUILD_IMAGE=$$build 			\
		--build-arg CMAKE_IMAGE=$$cmake 			\
		--build-arg FLB_VERSION=$$current 			\
		--build-arg OPENSSL_VERSION=$$openssl 		\
		--build-arg LIBYAML_VERSION=$$libyaml 		\
		--build-arg POSTGRES_VERSION=$$postgres 	\
		--build-arg AVRO_ENCODER=$$avro 			\
		--build-arg APT_DEPS="$(APT_DEPS)" 			\
		--tag $(IMAGE):$$current-dist 				\
		--tag $(IMAGE):$$short-dist 				\
		--progress plain $(BUILD_CONTEXT)


# docker build - итоговый образ с установленным ПО (установка из дистрибутива)
.PHONY: build
build: build-$(VERSION)

.ONESHELL:
build-%: $$(call need_build,$$*)
	@current=$(*)
	set -e
	set -- $(DEPS_$*)
	runtime=$$1; build=$$2; cmake=$$3; openssl=$$4; libyaml=$$5; postgres=$$6; avro=$$7
	short=$${current%.*}
	[ -z "$(call built,$(IMAGE):$*)" ] || { echo "  = $(IMAGE):$* уже собран (FORCE=1 — пересобрать)"; exit 0; }
	docker build -f Dockerfile $(NO_CACHE_TEXT) 	\
		--build-arg DIST_IMAGE=$(IMAGE):$*-dist	\
		--build-arg RUNTIME_IMAGE=$$runtime 		\
		--build-arg BUILD_IMAGE=$$build 			\
		--build-arg CMAKE_IMAGE=$$cmake 			\
		--build-arg FLB_VERSION=$$current 			\
		--build-arg OPENSSL_VERSION=$$openssl 		\
		--build-arg LIBYAML_VERSION=$$libyaml 		\
		--build-arg POSTGRES_VERSION=$$postgres 	\
		--build-arg AVRO_ENCODER=$$avro 			\
		--build-arg APT_DEPS="$(APT_DEPS)" 			\
		--tag $(IMAGE):$$current 					\
		--tag $(IMAGE):$$short 						\
		--progress plain $(BUILD_CONTEXT)


# тестирование собранного образа
.PHONY: test
test: test-$(VERSION)

.ONESHELL:
test-%: build-%
	@current=$(*)
	docker run --rm --entrypoint /bin/bash -v $(TEST_DIR):/test:ro $(IMAGE):$$current -euc '\
		echo "== version =="; 					\
		fluent-bit --version; 					\
		echo "== build info =="; 				\
		fluent-bit --help 2>&1 | head -1; 		\
		echo "== functional check =="; 			\
		/test/fluentbitcheck.sh'


# стенд для своих плагинов: настоящие postgres и clickhouse, проверка идёт
# по метрикам в prometheus_exporter, а не по факту запуска
STAND := $(TEST_DIR)/stand

.PHONY: stand
stand: build-$(VERSION)
	FLB_IMAGE=$(IMAGE):$(VERSION) docker compose -f $(STAND)/docker-compose.yml up -d
	$(STAND)/check.sh

.PHONY: stand-down
stand-down:
	docker compose -f $(STAND)/docker-compose.yml down -v


.PHONY: clean
clean: clean-$(VERSION)
	rm -rf "$(LINKED_ARTIFACTS)"

.ONESHELL:
clean-%:
	@current=$(*)
	set -e
	set -- $(DEPS_$*)
	openssl=$$4; libyaml=$$5; postgres=$$6
	rm -f "$(ARTIFACTS)/src/fluent-bit-$$current.tar.gz" \
		"$(ARTIFACTS)/src/openssl-$$openssl.tar.gz" \
		"$(ARTIFACTS)/src/yaml-$$libyaml.tar.gz" \
		"$(ARTIFACTS)/src/postgresql-$$postgres.tar.gz"


.PHONY: all
all: sources dist build test
