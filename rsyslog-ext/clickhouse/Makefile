BASE_DIR      	?= $(abspath .)
ARTIFACTS     	?= $(abspath $(BASE_DIR)/../../../ci-artifacts/rsyslog-ext/clickhouse)
BUILD_CONTEXT 	?= $(BASE_DIR)
# рабочая копия $(ARTIFACTS) внутри контекста сборки (хардлинки, см. artifacts-link)
LINKED_ARTIFACTS ?= $(BUILD_CONTEXT)/artifacts
TEST_DIR		?= $(BASE_DIR)/test

# путь, по которому модуль лежит внутри -dist образа (EXT_PATH в Dockerfile)
EXT_PATH		?= /opt/rsyslog-ext
# каталог модулей rsyslog на целевой машине (MODULE_DIR в Dockerfile)
MODULE_DIR		?= /usr/lib64/rsyslog


.DEFAULT_GOAL := help

# имя модуля (оно же суффикс тега образа)
EXT					:= clickhouse
# список версий rsyslog, которые поддерживает сборщик
# версии независимы, но собираются одна за другой, строго по цепочке
VERSION_CHAIN		?= 8.2602.0
# целевая версия сборки. по умолчанию берется последняя версия
VERSION      		?= $(lastword $(VERSION_CHAIN))
# базовые url адреса (конкретные версии подставляются в цели sources)
# rsyslog (files/download/rsyslog/rsyslog-<версия>.tar.gz)
BASE_URL			?= https://www.rsyslog.com/files/download/rsyslog
# libfastjson (libfastjson/libfastjson-<версия>.tar.gz)
LIBFASTJSON_URL		?= http://download.rsyslog.com
# curl (download/curl-<версия>.tar.gz)
CURL_URL			?= https://curl.se/download
# docker build --no-cache
NO_CACHE			?= 0
# apt-deps (build): libestr-dev и zlib1g-dev — обязательные зависимости rsyslog,
# libssl-dev — openssl для libcurl, pkg-config — им configure их и ищет.
# Пакетные версии подходят, из исходников собирается только то, что не подошло
# (см. комментарии в Dockerfile). Ставятся через apt: на машине сборки подложен
# sources.list с прокси nexus. Тарболы прокси не имеют — их качает `make sources`
APT_DEPS			?= pkg-config libestr-dev zlib1g-dev libssl-dev uuid-dev \
						libgcrypt20-dev libzstd-dev libgnutls28-dev librelp-dev libkrb5-dev \
						libsystemd-dev liblognorm-dev libmaxminddb-dev libpq-dev libdbi-dev \
						default-libmysqlclient-dev libsnmp-dev librdkafka-dev libnet1-dev \
						libhiredis-dev libmongoc-dev librabbitmq-dev libczmq-dev \
						libpcap-dev libjemalloc-dev liblogging-stdlog-dev \
						protobuf-c-compiler libprotobuf-c-dev libsnappy-dev tcl-dev

GLIBC_IMAGE			?= dmp/glibc:2.28
GCC_IMAGE			?= dmp/gcc:14.2.0-dist

# зависимости для сборки разных версий
# libfastjson: в astra 0.99.8, rsyslog 8.2602 требует >= 0.99.9
# curl: нужен только на линковке, поэтому версия заведомо старая —
# её символы есть в любой libcurl.so.4 новее
# версия      runtime_image     build_image     libfastjson  curl
DEPS_8.2602.0	:= $(GLIBC_IMAGE)   $(GCC_IMAGE)    1.2304.0     8.11.1

# базовое название для образа
IMAGE			:= dmp/rsyslog-$(EXT)
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

.PHONY: sources-all dist-all extract-all build-all test-all clean-all
sources-all:		$(addprefix sources-,$(VERSION_CHAIN))
dist-all:		$(addprefix dist-,$(VERSION_CHAIN))
extract-all:		$(addprefix extract-,$(VERSION_CHAIN))
build-all:		$(addprefix build-,$(VERSION_CHAIN))
test-all:		$(addprefix test-,$(VERSION_CHAIN))
clean-all:		$(addprefix clean-,$(VERSION_CHAIN))


.PHONY: help
help:
	@echo "make ($(EXT)):"
	@echo "  make all        собрать всё"
	@echo "  make sources    скачать исходники (rsyslog/libfastjson/curl) для $(VERSION)"
	@echo "  make dist       собрать -dist образ $(VERSION)"
	@echo "  make extract    выложить модуль из -dist образа в $(ARTIFACTS)/build/<версия>"
	@echo "  make build      собрать образ $(IMAGE)"
	@echo "  make test       проверить модуль: зависимости, загрузка в rsyslogd, путь через libcurl"
	@echo ""
	@echo "  Версии rsyslog: $(VERSION_CHAIN)"
	@echo "  Собирается omclickhouse из штатного дерева rsyslog (plugins/omclickhouse,"
	@echo "  апстрим держит его выключенным: --enable-clickhouse). Результат —"
	@echo "  $(MODULE_DIR)/omclickhouse.so для системного rsyslogd той же версии."
	@echo "  Модуль ставится к чужому rsyslogd, поэтому линкуется по минимуму:"
	@echo "  libcurl.so.4 + libc/libm/libpthread/libresolv, ни rpath, ни своих .so."
	@echo "  Сборка на dmp/glibc:2.28 — символы GLIBC_ не выше 2.14."
	@echo "  Зависимости берутся из штатного репозитория astra ($(APT_DEPS)),"
	@echo "  из исходников собирается только то, чего там нет подходящего:"
	@echo "    libfastjson  в astra 0.99.8, rsyslog требует >= 0.99.9"
	@echo "    curl         пакетный версионирует символы (CURL_OPENSSL_3),"
	@echo "                 в libcurl.so.4 из RED OS такой версии нет"
	@echo "  Эталонный rsyslog той же версии едет в образе — на нём и идёт make test."
	@echo "  Версии зависимостей — в DEPS_<версия>."
	@echo "  Другая версия: make all VERSION=8.2602.0"
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
	$(info EXT            = $(EXT))
	$(info VERSION_CHAIN  = $(VERSION_CHAIN))
	$(info VERSION        = $(VERSION))
	$(info FIRST_VERSION  = $(FIRST_VERSION))
	$(info BASE_URL       = $(BASE_URL))
	$(info LIBFASTJSON_URL = $(LIBFASTJSON_URL))
	$(info CURL_URL       = $(CURL_URL))
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
	libfastjson=$$3; curl=$$4
	mkdir -p $(ARTIFACTS)/src
	# полные url собираются из базового url и версии (версии — в DEPS_<версия>)
	[ -f "$(ARTIFACTS)/src/rsyslog-$$current.tar.gz" ] || curl -v -L -o "$(ARTIFACTS)/src/rsyslog-$$current.tar.gz" "$(BASE_URL)/rsyslog-$$current.tar.gz"
	[ -f "$(ARTIFACTS)/src/libfastjson-$$libfastjson.tar.gz" ] || curl -v -L -o "$(ARTIFACTS)/src/libfastjson-$$libfastjson.tar.gz" "$(LIBFASTJSON_URL)/libfastjson/libfastjson-$$libfastjson.tar.gz"
	[ -f "$(ARTIFACTS)/src/curl-$$curl.tar.gz" ] || curl -v -L -o "$(ARTIFACTS)/src/curl-$$curl.tar.gz" "$(CURL_URL)/curl-$$curl.tar.gz"


# docker build --target dist - дистрибутив со скомпилированными исходниками (без установки в систему)
.PHONY: dist
dist: dist-$(VERSION)

.ONESHELL:
dist-%: $$(call need_dist,$$*)
	@current=$(*)
	set -e
	set -- $(DEPS_$*)
	runtime=$$1; build=$$2; libfastjson=$$3; curl=$$4
	short=$${current%.*}
	[ -z "$(call built,$(IMAGE):$*-dist)" ] || { echo "  = $(IMAGE):$*-dist уже собран (FORCE=1 — пересобрать)"; exit 0; }
	docker build -f Dockerfile $(NO_CACHE_TEXT) --target dist \
		--build-arg RUNTIME_IMAGE=$$runtime 			\
		--build-arg BUILD_IMAGE=$$build 				\
		--build-arg RSYSLOG_VERSION=$$current 			\
		--build-arg LIBFASTJSON_VERSION=$$libfastjson 	\
		--build-arg CURL_VERSION=$$curl 				\
		--build-arg APT_DEPS="$(APT_DEPS)" 				\
		--tag $(IMAGE):$$current-dist 					\
		--tag $(IMAGE):$$short-dist 					\
		--progress plain $(BUILD_CONTEXT)


# извлечь модуль ($(EXT_PATH)) из -dist образа в artifacts/build/<версия>:
# отсюда его и переносят на целевую машину
.PHONY: extract
extract: extract-$(VERSION)

.ONESHELL:
extract-%: dist-%
	@current=$(*)
	set -e
	img='$(IMAGE):'"$$current"'-dist'
	dst='$(ARTIFACTS)/build/'"$$current"
	# создаём временный контейнер из -dist образа (без запуска) и копируем из него дерево.
	# команда обязательна: у сборочного образа своей нет, а docker create её требует
	cid=$$(docker create $$img /bin/true)
	trap 'docker rm -f $$cid >/dev/null 2>&1 || true' EXIT
	rm -rf "$$dst"
	mkdir -p "$$dst"
	docker cp "$$cid:$(EXT_PATH)/." "$$dst/"
	echo "extracted $$img:$(EXT_PATH) -> $$dst"
	find "$$dst" -type f | sort


# docker build - итоговый образ с установленным ПО (установка из дистрибутива)
.PHONY: build
build: build-$(VERSION)

.ONESHELL:
build-%: $$(call need_build,$$*)
	@current=$(*)
	set -e
	set -- $(DEPS_$*)
	runtime=$$1; build=$$2; libfastjson=$$3; curl=$$4
	short=$${current%.*}
	[ -z "$(call built,$(IMAGE):$*)" ] || { echo "  = $(IMAGE):$* уже собран (FORCE=1 — пересобрать)"; exit 0; }
	docker build -f Dockerfile $(NO_CACHE_TEXT) 		\
		--build-arg DIST_IMAGE=$(IMAGE):$*-dist			\
		--build-arg RUNTIME_IMAGE=$$runtime 			\
		--build-arg BUILD_IMAGE=$$build 				\
		--build-arg RSYSLOG_VERSION=$$current 			\
		--build-arg LIBFASTJSON_VERSION=$$libfastjson 	\
		--build-arg CURL_VERSION=$$curl 				\
		--build-arg APT_DEPS="$(APT_DEPS)" 				\
		--tag $(IMAGE):$$current 						\
		--tag $(IMAGE):$$short 							\
		--progress plain $(BUILD_CONTEXT)


# тестирование собранного образа
.PHONY: test
test: test-$(VERSION)

.ONESHELL:
test-%: build-%
	@current=$(*)
	docker run --rm --entrypoint /bin/bash -v $(TEST_DIR):/test:ro $(IMAGE):$$current -euc '\
		echo "== version =="; 					\
		rsyslogd -v | head -1; 					\
		echo "== functional check =="; 			\
		/test/check.sh'


.PHONY: clean
clean: clean-$(VERSION)
	rm -rf "$(LINKED_ARTIFACTS)"

.ONESHELL:
clean-%:
	@current=$(*)
	set -e
	set -- $(DEPS_$*)
	libfastjson=$$3; curl=$$4
	rm -f "$(ARTIFACTS)/src/rsyslog-$$current.tar.gz" \
		"$(ARTIFACTS)/src/libfastjson-$$libfastjson.tar.gz" \
		"$(ARTIFACTS)/src/curl-$$curl.tar.gz"
	rm -rf "$(ARTIFACTS)/build/$$current"


.PHONY: all
all: sources dist build test
