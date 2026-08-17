BASE_DIR      	?= $(abspath .)
ARTIFACTS     	?= $(abspath $(BASE_DIR)/../../ci-artifacts/sqlite)
BUILD_CONTEXT 	?= $(BASE_DIR)
# рабочая копия $(ARTIFACTS) внутри контекста сборки (хардлинки, см. artifacts-link)
LINKED_ARTIFACTS ?= $(BUILD_CONTEXT)/artifacts
TEST_DIR		?= $(BASE_DIR)/test

# путь, по которому дистрибутив лежит внутри -dist образа (SQLITE_PATH в Dockerfile)
SQLITE_PATH		?= /opt/sqlite


.DEFAULT_GOAL := help

# список версий, которые поддерживает сборщик
# версии независимы друг от друга, собираются по отдельности
VERSION_CHAIN		?= 3.53.4
# целевая версия сборки. по умолчанию берется последняя версия
VERSION      		?= $(lastword $(VERSION_CHAIN))
# базовый url адрес для скачивания исходников собираемых в Dockerfile
BASE_URL			?= https://sqlite.org
# зеркало на случай недоступности sqlite.org
MIRROR_URL			?= https://sources.buildroot.net/sqlite
# выполнять ли сброс кэша: docker build --no-cache
NO_CACHE			?= 0

GLIBC_IMAGE			?= dmp/glibc:2.28
GCC_IMAGE			?= dmp/gcc:12.4.0-dist

# зависимости для сборки разных версии
# amalgamation: версия в формате тарбола (3.53.4 -> 3530400)
# year:         каталог года на sqlite.org
# узнать значения для новой версии: make deps VERSION=<версия>
# версия           runtime_image		build_image		amalgamation	year	sha256
DEPS_3.53.4		:= $(GLIBC_IMAGE)	$(GCC_IMAGE)	3530400			2026	0e9483900e92cd5de8fd48d16bf9200145a61f7fd5be542a5ac81d8a9516eb9c

# базовое название для образа
IMAGE			:= dmp/sqlite
FIRST_VERSION	:= $(firstword $(VERSION_CHAIN))
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

.PHONY: build-all clean-all deps-all dist-all extract-all sources-all test-all
build-all:		$(addprefix build-,$(VERSION_CHAIN))
clean-all:		$(addprefix clean-,$(VERSION_CHAIN))
deps-all:		$(addprefix deps-,$(VERSION_CHAIN))
dist-all:		$(addprefix dist-,$(VERSION_CHAIN))
extract-all:		$(addprefix extract-,$(VERSION_CHAIN))
sources-all:		$(addprefix sources-,$(VERSION_CHAIN))
test-all:		$(addprefix test-,$(VERSION_CHAIN))


.PHONY: help
help:
	@echo "make:"
	@echo "  make all        собрать всё"
	@echo "  make sources    скачать исходники версии $(VERSION)"
	@echo "  make deps       показать amalgamation/год/sha256 версии $(VERSION) (для переноса в DEPS)"
	@echo "  make dist       собрать $(IMAGE):$(VERSION)-dist"
	@echo "  make extract    достать дистрибутив ($(SQLITE_PATH)) из -dist образа в $(ARTIFACTS)/build/<версия>"
	@echo "  make build      собрать образ $(IMAGE)"
	@echo "  make test       проверить образ: версия, compile options, FTS5 trigram, WAL"
	@echo ""
	@echo "  Версии: $(VERSION_CHAIN)"
	@echo "  Собирается amalgamation-тарбол с набором SQLITE_ENABLE_*, который"
	@echo "  требует hermes-agent: FTS5 (tokenize='trigram'), RTREE, GEOPOLY,"
	@echo "  SESSION, MATH_FUNCTIONS, THREADSAFE=1."
	@echo "  Зависимости задаются пословно в DEPS_<версия>."
	@echo "  Другая версия: make all VERSION=3.53.4"
	@echo ""


.PHONY: debug
debug:
	$(info BASE_DIR       = $(BASE_DIR))
	$(info ARTIFACTS      = $(ARTIFACTS))
	$(info SQLITE_PATH    = $(SQLITE_PATH))
	$(info BUILD_CONTEXT  = $(BUILD_CONTEXT))
	$(info LINKED_ARTIFACTS = $(LINKED_ARTIFACTS))
	$(info TEST_DIR       = $(TEST_DIR))
	$(info VERSION_CHAIN  = $(VERSION_CHAIN))
	$(info VERSION        = $(VERSION))
	$(info FIRST_VERSION  = $(FIRST_VERSION))
	$(info BASE_URL       = $(BASE_URL))
	$(info MIRROR_URL     = $(MIRROR_URL))
	$(info NO_CACHE       = $(NO_CACHE))
	$(info NO_CACHE_TEXT  = $(NO_CACHE_TEXT))
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


# Скачиваем исходники для сборки дистрибутива
.PHONY: sources
sources: sources-$(VERSION)

.ONESHELL:
sources-%:
	@current=$(*)
	set -e
	set -- $(DEPS_$*)
	mkdir -p $(ARTIFACTS)/src
	name="sqlite-autoconf-$$3.tar.gz"
	dst="$(ARTIFACTS)/src/$$name"
	if [ ! -f "$$dst" ]; then
		curl -fvL -o "$$dst" "$(BASE_URL)/$$4/$$name" \
			|| curl -fvL -o "$$dst" "$(MIRROR_URL)/$$name"
	fi
	echo "$$5  $$dst" | sha256sum -c -


# Показывает amalgamation, год и sha256 тарбола. Числа отсюда переносятся
# руками в DEPS_<версия>. Работает и для версии, которой еще нет в DEPS
.PHONY: deps
deps: deps-$(VERSION)

.ONESHELL:
deps-%:
	@current=$(*)
	set -e
	amalgamation=$$(echo "$$current" | awk -F. '{printf "%d%02d%02d00\n", $$1, $$2, $$3}')
	tmp=$$(mktemp -d)
	trap 'rm -rf $$tmp' EXIT
	year=""
	for y in $$(date +%Y) $$(($$(date +%Y) - 1)) $$(($$(date +%Y) - 2)); do
		if curl -fsIL "$(BASE_URL)/$$y/sqlite-autoconf-$$amalgamation.tar.gz" >/dev/null 2>&1; then
			year=$$y; break
		fi
	done
	[ -n "$$year" ] || { echo "sqlite-autoconf-$$amalgamation.tar.gz не найден на $(BASE_URL)" >&2; exit 1; }
	curl -fsL -o "$$tmp/src.tar.gz" "$(BASE_URL)/$$year/sqlite-autoconf-$$amalgamation.tar.gz"
	sha=$$(sha256sum "$$tmp/src.tar.gz" | cut -d' ' -f1)
	echo "sqlite $$current:"
	echo "  amalgamation:  $$amalgamation"
	echo "  year:          $$year"
	echo "  sha256:        $$sha"
	echo ""
	echo "строка для DEPS (образы подставьте свои):"
	printf 'DEPS_%s\t:= $$(GLIBC_IMAGE)\t$$(GCC_IMAGE)\t%s\t\t\t%s\t%s\n' \
		"$$current" "$$amalgamation" "$$year" "$$sha"


# docker build --target dist - дистрибутив с скомпилированными исходниками (без установки в систему)
.PHONY: dist
dist: dist-$(VERSION)

.ONESHELL:
dist-%: $$(call need_dist,$$*)
	@current=$(*)
	set -e
	set -- $(DEPS_$*)
	[ -z "$(call built,$(IMAGE):$*-dist)" ] || { echo "  = $(IMAGE):$*-dist уже собран (FORCE=1 — пересобрать)"; exit 0; }
	docker build -f Dockerfile $(NO_CACHE_TEXT) --target dist	\
		--build-arg RUNTIME_IMAGE=$$1				\
		--build-arg BUILD_IMAGE=$$2					\
		--build-arg SQLITE_VERSION=$$current		\
		--build-arg SQLITE_AMALGAMATION=$$3			\
		--tag $(IMAGE):$$current-dist				\
		--progress plain $(BUILD_CONTEXT)


# извлечь скомпилированный дистрибутив (/opt/sqlite) из -dist образа
# в artifacts/build/<версия>
.PHONY: extract
extract: extract-$(VERSION)

.ONESHELL:
extract-%: dist-%
	@current=$(*)
	set -e
	img='$(IMAGE):'"$$current"'-dist'
	dst='$(ARTIFACTS)/build/'"$$current"
	# создаём временный контейнер из -dist образа (без запуска) и копируем из него дерево.
	# команда обязательна: у -dist образа нет ни ENTRYPOINT, ни CMD
	cid=$$(docker create $$img /bin/true)
	trap 'docker rm -f $$cid >/dev/null 2>&1 || true' EXIT
	rm -rf "$$dst"
	mkdir -p "$$dst"
	docker cp "$$cid:$(SQLITE_PATH)/." "$$dst/"
	echo "extracted $$img:$(SQLITE_PATH) -> $$dst"
	du -h --max-depth=1 "$$dst" || true


# docker build - истоговый образ с установленным ПО (установка из дистрибутива)
.PHONY: build
build: build-$(VERSION)

.ONESHELL:
build-%: $$(call need_build,$$*)
	@current=$(*)
	set -e
	set -- $(DEPS_$*)
	major=$${current%.*}
	[ -z "$(call built,$(IMAGE):$*)" ] || { echo "  = $(IMAGE):$* уже собран (FORCE=1 — пересобрать)"; exit 0; }
	docker build -f Dockerfile $(NO_CACHE_TEXT)		\
		--build-arg RUNTIME_IMAGE=$$1				\
		--build-arg BUILD_IMAGE=$$2					\
		--build-arg SQLITE_VERSION=$$current		\
		--build-arg SQLITE_AMALGAMATION=$$3			\
		--tag $(IMAGE):$$current					\
		--tag $(IMAGE):$$major						\
		--progress plain $(BUILD_CONTEXT)


# тестирование собранного образа
.PHONY: test
test: test-$(VERSION)

.ONESHELL:
test-%: build-%
	@current=$(*)
	docker run --rm --entrypoint /bin/bash -e EXPECT=$$current \
		-v $(TEST_DIR):/test:ro $(IMAGE):$$current -euc '\
		echo "== versions =="; 					\
		sqlite3 --version; 						\
		test "$$(sqlite3 :memory: "select sqlite_version()")" = "$$EXPECT"; \
		echo "  ok    sqlite_version() = $$EXPECT"; \
		echo "== library =="; 					\
		ls -l /usr/local/lib/libsqlite3.so*; 	\
		ldconfig -p | grep libsqlite3; 			\
		echo "== compile options =="; 			\
		sqlite3 :memory: "select * from pragma_compile_options()" | sort; \
		echo "== features =="; 					\
		rm -f /tmp/test.db; 					\
		sqlite3 /tmp/test.db < /test/features.sql'


.PHONY: clean
clean: clean-$(VERSION)
	rm -rf "$(LINKED_ARTIFACTS)"

.ONESHELL:
clean-%:
	@current=$(*)
	set -e
	set -- $(DEPS_$*)
	rm -f "$(ARTIFACTS)/src/sqlite-autoconf-$$3.tar.gz"


.PHONY: all
all: sources dist build test
