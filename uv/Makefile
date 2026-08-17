BASE_DIR      	?= $(abspath .)
ARTIFACTS     	?= $(abspath $(BASE_DIR)/../../ci-artifacts/uv)
BUILD_CONTEXT 	?= $(BASE_DIR)
# рабочая копия $(ARTIFACTS) внутри контекста сборки (хардлинки, см. artifacts-link)
LINKED_ARTIFACTS ?= $(BUILD_CONTEXT)/artifacts
TEST_DIR		?= $(BASE_DIR)/test


.DEFAULT_GOAL := help

# список версий, которые поддерживает сборщик
# версии собираются одна за другой, строго по цепочке
# версии uv друг от друга не зависят (в отличие от rust), цепочка нужна только
# чтобы `make all` собрал их все — по одной на каждый rustc из проекта rust
VERSION_CHAIN		?= 0.9.26 0.9.30 0.10.12 0.11.19 0.11.33
# целевая версия сборки. по умолчанию берется последняя версия
VERSION      		?= $(lastword $(VERSION_CHAIN))
# базовый url адрес для скачивания исходников собираемых в Dockerfile
BASE_URL			?= https://github.com/astral-sh/uv/archive/refs/tags
# базовый url адрес для чтения манифестов репозитория, только для make deps
RAW_BASE_URL		?= https://raw.githubusercontent.com/astral-sh/uv
# выполнять ли сброс кэша: docker build --no-cache
NO_CACHE			?= 0

GLIBC_IMAGE			?= dmp/glibc:2.28
RUST_IMAGE			?= dmp/rust:1.90.0
# интерпретатор для make test, в рантайме его нет
PYTHON_IMAGE		?= dmp/python:3.11
APT_DEPS			?=

# CARGO_HOME внутри $(RUST_IMAGE)
CARGO_HOME			?= /usr/local/cargo
# cargo-config закрытого контура, монтируется если файл есть
CARGO_CONF_MOUNT	:= $(if $(wildcard $(BASE_DIR)/cargo-config.toml),-v "$(BASE_DIR)/cargo-config.toml":$(CARGO_HOME)/config.toml:ro,)

# build_image задаёт MSRV из Cargo.toml исходников: make deps VERSION=<версия>
# зависимости для сборки разных версии
# версия           runtime_image	build_image			MSRV
DEPS_0.9.0		:= $(GLIBC_IMAGE)	dmp/rust:1.90.0		# 1.88
DEPS_0.9.15		:= $(GLIBC_IMAGE)	dmp/rust:1.90.0		# 1.89
DEPS_0.9.26		:= $(GLIBC_IMAGE)	dmp/rust:1.90.0		# 1.89
DEPS_0.9.30		:= $(GLIBC_IMAGE)	dmp/rust:1.91.0		# 1.91
DEPS_0.10.11	:= $(GLIBC_IMAGE)	dmp/rust:1.92.0		# 1.92
DEPS_0.10.12	:= $(GLIBC_IMAGE)	dmp/rust:1.92.0		# 1.92
DEPS_0.11.19	:= $(GLIBC_IMAGE)	dmp/rust:1.94.0		# 1.94
DEPS_0.11.31	:= $(GLIBC_IMAGE)	dmp/rust:1.95.0		# 1.95
DEPS_0.11.32	:= $(GLIBC_IMAGE)	dmp/rust:1.95.0		# 1.95
DEPS_0.11.33	:= $(GLIBC_IMAGE)	dmp/rust:1.95.0		# 1.95

# базовое название для образа
IMAGE			:= dmp/uv
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

.PHONY: sources-all deps-all dist-all build-all test-all clean-all
sources-all:		$(addprefix sources-,$(VERSION_CHAIN))
deps-all:		$(addprefix deps-,$(VERSION_CHAIN))
dist-all:		$(addprefix dist-,$(VERSION_CHAIN))
build-all:		$(addprefix build-,$(VERSION_CHAIN))
test-all:		$(addprefix test-,$(VERSION_CHAIN))
clean-all:		$(addprefix clean-,$(VERSION_CHAIN))


.PHONY: help
help:
	@echo "make:"
	@echo "  make all        собрать всё"
	@echo "  make deps       показать версию rust, которую требуют исходники $(VERSION) (для переноса в DEPS)"
	@echo "  make sources    скачать исходники + завендорить крейты для $(VERSION)"
	@echo "  make dist       собрать -dist образ $(VERSION) (звенья цепочки — если нужны)"
	@echo "  make build      собрать образ $(IMAGE)"
	@echo "  make test       проверить образ $(IMAGE): версии, порог glibc, uv lock/venv без сети"
	@echo "  make glibc      показать максимальную требуемую версию символов GLIBC_ у бинаря"
	@echo ""
	@echo "  Цепочка: $(VERSION_CHAIN)"
	@echo "  Каждая версия собирается тулчейном $(RUST_IMAGE), рантайм — $(GLIBC_IMAGE)."
	@echo "  sources вендорит крейты (cargo vendor через $(RUST_IMAGE) с сетью) -> оффлайн-сборка."
	@echo "  Зависимости (runtime_image/build_image) задаются пословно в DEPS_<версия>."
	@echo "  Потолок версии uv задаёт rustc: см. таблицу MSRV в Makefile."
	@echo "  Другая версия: make all VERSION=0.9.0"
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
	$(info RAW_BASE_URL   = $(RAW_BASE_URL))
	$(info RUST_IMAGE     = $(RUST_IMAGE))
	$(info PYTHON_IMAGE   = $(PYTHON_IMAGE))
	$(info CARGO_HOME     = $(CARGO_HOME))
	$(info CARGO_CONF_MOUNT = $(if $(CARGO_CONF_MOUNT),$(CARGO_CONF_MOUNT),<нет cargo-config.toml>))
	$(info GLIBC_IMAGE    = $(GLIBC_IMAGE))
	$(info APT_DEPS       = $(APT_DEPS))
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


# Показывает версию rust, которую требуют исходники указанной версии: она зашита
# в Cargo.toml (rust-version, MSRV). Число отсюда переносится руками в
# DEPS_<версия> выбором build_image. Работает и для версии, которой еще нет
# в DEPS — манифест берет из завендоренного тарбола или с github
.PHONY: deps
deps: deps-$(VERSION)

.ONESHELL:
deps-%:
	@current=$(*)
	set -e
	mkdir -p $(ARTIFACTS)/src
	src="$(ARTIFACTS)/src/uv-$$current.tar.gz"
	if [ -f "$$src" ]; then
		manifest=$$(tar -xOf "$$src" "uv-$$current/Cargo.toml")
		toolchain=$$(tar -xOf "$$src" "uv-$$current/rust-toolchain.toml")
	else
		manifest=$$(curl -fsSL "$(RAW_BASE_URL)/$$current/Cargo.toml")
		toolchain=$$(curl -fsSL "$(RAW_BASE_URL)/$$current/rust-toolchain.toml")
	fi
	msrv=$$(printf '%s\n' "$$manifest"  | grep -m1 '^rust-version' | tr -d ' "' | cut -d= -f2)
	pin=$$(printf '%s\n'  "$$toolchain" | grep -m1 'channel'       | tr -d ' "' | cut -d= -f2)
	echo "uv $$current:"
	echo "  MSRV (Cargo.toml):    $$msrv  -> нужен dmp/rust не ниже"
	echo "  rust-toolchain.toml:  $$pin  (пин CI, читает rustup — у нас его нет, cargo файл игнорирует)"
	# если версия уже есть в DEPS — сверяем с rustc из build-образа
	set -- $(DEPS_$*)
	if [ -n "$$2" ]; then
		rustc_ver=$$(docker run --rm --entrypoint rustc $$2 --version | awk '{print $$2}' | cut -d- -f1)
		echo "  build_image $$2: rustc $$rustc_ver"
		lowest=$$(printf '%s\n%s\n' "$$msrv" "$$rustc_ver" | sort -V | head -1)
		if [ "$$lowest" = "$$msrv" ]; then echo "  OK: rustc $$rustc_ver >= MSRV $$msrv"
		else echo "  FAIL: rustc $$rustc_ver < MSRV $$msrv — продлите цепочку astra-linux-based/rust"; exit 1; fi
	fi
	echo ""
	echo "строка для DEPS (образы подставьте свои):"
	printf 'DEPS_%s\t:= $$(GLIBC_IMAGE)\t$$(RUST_IMAGE)\t# MSRV %s\n' "$$current" "$$msrv"


# Скачиваем исходники и вендорим крейты для оффлайн-сборки
.PHONY: sources
sources: sources-$(VERSION)

.ONESHELL:
sources-%:
	@current=$(*)
	set -e
	set -- $(DEPS_$*)
	mkdir -p $(ARTIFACTS)/src
	# уже завендорено — пропускаем
	[ -f "$(ARTIFACTS)/src/uv-$$current.tar.gz" ] && exit 0 || true
	# скачиваем исходники, вендорим крейты (rust-образ с сетью), пакуем вместе с vendor/
	# vendor/ пишется от root -> распаковку, вендоринг, упаковку и уборку делаем ВНУТРИ контейнера
	tmp=$$(mktemp -d)
	curl -v -L -o "$$tmp/uv.tar.gz" "$(BASE_URL)/$$current.tar.gz"
	# .cargo/config.toml в uv уже есть -> дописываем, а не перезаписываем
	docker run --rm -e CUR=$$current $(CARGO_CONF_MOUNT) -v "$$tmp":/w -v "$(ARTIFACTS)/src":/out --entrypoint sh $$2 -c \
		'set -e; cd /w; tar -xf uv.tar.gz; cd uv-$$CUR; mkdir -p .cargo; cargo vendor --versioned-dirs --locked vendor >> .cargo/config.toml; rm -rf target .git; du -sh vendor; tar -C /w -czf /out/uv-$$CUR.tar.gz uv-$$CUR; rm -rf /w/uv-$$CUR'
	rm -rf "$$tmp"
	ls -lh "$(ARTIFACTS)/src/uv-$$current.tar.gz"


# docker build --target dist - дистрибутив с скомпилированными исходниками (без установки в систему)
.PHONY: dist
dist: dist-$(VERSION)

.ONESHELL:
dist-%: $$(call need_dist,$$*)
	@current=$(*)
	set -e
	set -- $(DEPS_$*)
	[ -z "$(call built,$(IMAGE):$*-dist)" ] || { echo "  = $(IMAGE):$*-dist уже собран (FORCE=1 — пересобрать)"; exit 0; }
	docker build -f Dockerfile $(NO_CACHE_TEXT) --target dist \
		--build-arg RUNTIME_IMAGE=$$1		\
		--build-arg BUILD_IMAGE=$$2 		\
		--build-arg UV_VERSION=$$current	\
		--build-arg APT_DEPS="$(APT_DEPS)"	\
		--tag $(IMAGE):$$current-dist 		\
		--progress plain $(BUILD_CONTEXT)


# docker build - истоговый образ с установленным ПО (установка из дистрибутива)
.PHONY: build
build: build-$(VERSION)

.ONESHELL:
build-%: $$(call need_build,$$*)
	@current=$(*)
	set -e
	set -- $(DEPS_$*)
	[ -z "$(call built,$(IMAGE):$*)" ] || { echo "  = $(IMAGE):$* уже собран (FORCE=1 — пересобрать)"; exit 0; }
	docker build -f Dockerfile $(NO_CACHE_TEXT) \
		--build-arg DIST_IMAGE=$(IMAGE):$*-dist	\
		--build-arg RUNTIME_IMAGE=$$1		\
		--build-arg BUILD_IMAGE=$$2			\
		--build-arg UV_VERSION=$$current	\
		--build-arg APT_DEPS="$(APT_DEPS)"	\
		--tag $(IMAGE):$$current			\
		--progress plain $(BUILD_CONTEXT)


# максимальная версия символа GLIBC_, которую требуют бинари — ниже неё не поедут.
# считается на этапе builder, в рантайме нет binutils
.PHONY: glibc
glibc: glibc-$(VERSION)

.ONESHELL:
glibc-%:
	@current=$(*)
	docker run --rm --entrypoint /bin/cat $(IMAGE):$$current /usr/local/share/uv/glibc-floor.txt


# тестирование собранного образа
.PHONY: test
test: test-$(VERSION)

.ONESHELL:
test-%: build-%
	@current=$(*)
	set -e
	docker run --rm --entrypoint /bin/bash $(IMAGE):$$current -euc '\
		echo "== versions ==";							\
		uv --version;									\
		uvx --version;									\
		echo "== linkage ==";							\
		ldd /usr/local/bin/uv;							\
		echo "== glibc floor ==";						\
		cat /usr/local/share/uv/glibc-floor.txt;		\
		echo "== dirs ==";								\
		uv cache dir;									\
		uv python dir;									\
		uv tool dir;									\
		echo "== python list ==";						\
		uv python list | head -3'
	# в рантайме нет интерпретатора, поэтому uv достается из образа и
	# запускается в $(PYTHON_IMAGE) с отключенной сетью
	tmp=$$(mktemp -d)
	cid=$$(docker create $(IMAGE):$$current)
	docker cp $$cid:/usr/local/bin/uv "$$tmp/uv"
	docker rm $$cid > /dev/null
	docker run --rm --network none -v "$$tmp":/uvbin:ro -v $(TEST_DIR):/test:ro --entrypoint bash $(PYTHON_IMAGE) -euc '\
		cp -r /test/demo /tmp/demo && cd /tmp/demo;						\
		echo "== uv lock без сети ==";									\
		/uvbin/uv lock --offline --python "$$(command -v python3)";		\
		cat uv.lock;													\
		echo "== uv venv без сети ==";									\
		/uvbin/uv venv --offline --python "$$(command -v python3)" /tmp/venv'
	rm -rf "$$tmp"
	echo "OK"


.PHONY: clean
clean: clean-$(VERSION)
	rm -rf "$(LINKED_ARTIFACTS)"

.ONESHELL:
clean-%:
	@current=$(*)
	set -e
	mkdir -p $(ARTIFACTS)/src
	rm -f "$(ARTIFACTS)/src/uv-$$current.tar.gz"


.PHONY: all
all: sources dist build test
