BASE_DIR      	?= $(abspath .)
ARTIFACTS     	?= $(abspath $(BASE_DIR)/../../ci-artifacts/rust)
BUILD_CONTEXT 	?= $(BASE_DIR)
# рабочая копия $(ARTIFACTS) внутри контекста сборки (хардлинки, см. artifacts-link)
LINKED_ARTIFACTS ?= $(BUILD_CONTEXT)/artifacts
TEST_DIR		?= $(BASE_DIR)/test


.DEFAULT_GOAL := help

# список версий, которые поддерживает сборщик
# версии собираются одна за другой, строго по цепочке
VERSION_CHAIN		?= 1.90.0 1.91.0 1.92.0 1.93.0 1.94.0 1.95.0
# целевая версия сборки. по умолчанию берется последняя версия
# соответственно для ее сборки потребуется собрать все предыдущие версии в цепочки
VERSION      		?= $(lastword $(VERSION_CHAIN))
# базовый url адрес для скачивания исходников собираемых в Dockerfile
RUST_SRC_BASE		?= https://static.rust-lang.org/dist
MRUSTC_SRC_BASE		?= https://github.com/thepowersgang/mrustc/archive/refs/tags
NINJA_SRC_BASE		?= https://github.com/ninja-build/ninja/archive/refs/tags
# ninja нужен только ветке bootstrap: им собирается llvm из состава rustc
NINJA_VERSION		?= 1.11.1
APT_DEPS			?= patch curl git zlib1g-dev libssl-dev pkg-config
# выполнять ли сброс кэша: docker build --no-cache
NO_CACHE			?= 0
# сколько rustc гонять параллельно на этапе run_rustc первого звена (mrustc).
# пусто = min(nproc, 8): rustc там собран самим mrustc, неоптимизирован и жрёт
# память, при -j nproc на машине с малым объёмом RAM на ядро падает по SIGABRT
MRUSTC_JOBS			?=
# пересобирать звено цепочки, даже если образ с таким тегом уже есть.
# по умолчанию готовые звенья пропускаются: цепочка длинная, каждое звено — часы,
# и любая правка Dockerfile иначе тянет за собой пересборку всех предыдущих
FORCE				?= 0

GCC_IMAGE			?= dmp/gcc:8.5.0
# llvm 21+ (идёт в составе rustc 1.91+) старым gcc уже не собирается
GCC_NEW_IMAGE		?= dmp/gcc:14.2.0
CMAKE_IMAGE			?= dmp/cmake:3.27.9-dist
PYTHON_IMAGE		?= dmp/python:3.10-dist

# Первое звено цепочки поднимает mrustc (компилятора rust ещё нет) — это сутки
# сборки. Дальше обычный self-hosting: rustc N-1 собирает rustc N через x.py,
# build_image — предыдущий -dist образ, из него берётся stage0. Прочерк в поле
# mrustc и означает «эта версия собирается bootstrap-ом».
# Версии пропускать нельзя: stage0 должен быть ровно предыдущим минором,
# поэтому 1.93.0 в цепочке есть, хотя сам по себе никому не нужен.
# зависимости для сборки разных версии
# версия       runtime_image   		build_image     		cmake_image     python_image    mrustc
DEPS_1.90.0	:= $(GCC_IMAGE)			$(GCC_IMAGE)			$(CMAKE_IMAGE)	$(PYTHON_IMAGE)	0.12.0
DEPS_1.91.0	:= $(GCC_NEW_IMAGE)		dmp/rust:1.90.0-dist	$(CMAKE_IMAGE)	$(PYTHON_IMAGE)	-
DEPS_1.92.0	:= $(GCC_NEW_IMAGE)		dmp/rust:1.91.0-dist	$(CMAKE_IMAGE)	$(PYTHON_IMAGE)	-
DEPS_1.93.0	:= $(GCC_NEW_IMAGE)		dmp/rust:1.92.0-dist	$(CMAKE_IMAGE)	$(PYTHON_IMAGE)	-
DEPS_1.94.0	:= $(GCC_NEW_IMAGE)		dmp/rust:1.93.0-dist	$(CMAKE_IMAGE)	$(PYTHON_IMAGE)	-
DEPS_1.95.0	:= $(GCC_NEW_IMAGE)		dmp/rust:1.94.0-dist	$(CMAKE_IMAGE)	$(PYTHON_IMAGE)	-

# базовое название для образа
IMAGE			:= dmp/rust
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
	@echo "  make sources    скачать исходники для $(VERSION)"
	@echo "  make dist       собрать -dist образ $(VERSION) (звенья цепочки — если нужны)"
	@echo "  make build      собрать образ $(IMAGE)"
	@echo "  make test       проверить тулчейн: собрать и запустить test/hello.rs в образе $(IMAGE)"
	@echo ""
	@echo "  Цепочка: $(VERSION_CHAIN)"
	@echo "  Первая версия цепочки собирается ИЗ ИСХОДНИКОВ через mrustc (C++), без бинарного rustc."
	@echo "  Остальные — обычным bootstrap-ом (x.py): rustc N-1 из предыдущего -dist образа собирает rustc N."
	@echo "  Зависимости (cmake/python/mrustc) задаются пословно в DEPS_<версия>, прочерк в поле mrustc = bootstrap."
	@echo "  Уже собранные звенья пропускаются, пересобрать: FORCE=1"
	@echo "  Первое звено проще перенести с другой машины (docker save/load), чем пересобирать."
	@echo "  Если mrustc-этап падает по SIGABRT (мало памяти) — уменьшить: MRUSTC_JOBS=2"
	@echo "  Другая версия: make all VERSION=1.90.0"
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
	$(info RUST_SRC_BASE  = $(RUST_SRC_BASE))
	$(info MRUSTC_SRC_BASE = $(MRUSTC_SRC_BASE))
	$(info NINJA_SRC_BASE = $(NINJA_SRC_BASE))
	$(info NINJA_VERSION  = $(NINJA_VERSION))
	$(info APT_DEPS       = $(APT_DEPS))
	$(info NO_CACHE       = $(NO_CACHE))
	$(info FORCE          = $(FORCE))
	$(info MRUSTC_JOBS    = $(MRUSTC_JOBS))
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


# Скачиваем зависимости для сборки дистрибутива
.PHONY: sources
sources: sources-$(VERSION)

.ONESHELL:
sources-%:
	@current=$(*)
	set -e
	set -- $(DEPS_$*)
	mkdir -p $(ARTIFACTS)/src
	if [ "$$5" = "-" ]; then
		# ветка bootstrap: вместо mrustc нужен ninja, им собирается llvm
		[ -f "$(ARTIFACTS)/src/ninja-$(NINJA_VERSION).tar.gz" ] || curl -v -L -o "$(ARTIFACTS)/src/ninja-$(NINJA_VERSION).tar.gz" "$(NINJA_SRC_BASE)/v$(NINJA_VERSION).tar.gz"
	else
		[ -f "$(ARTIFACTS)/src/mrustc-$$5.tar.gz" ] || curl -v -L -o "$(ARTIFACTS)/src/mrustc-$$5.tar.gz" "$(MRUSTC_SRC_BASE)/v$$5.tar.gz"
	fi
	[ -f "$(ARTIFACTS)/src/rustc-$$current-src.tar.gz" ] || curl -v -o "$(ARTIFACTS)/src/rustc-$$current-src.tar.gz" "$(RUST_SRC_BASE)/rustc-$$current-src.tar.gz"


# docker build --target dist - дистрибутив с скомпилированными исходниками (без установки в систему)
.PHONY: dist
dist: dist-$(VERSION)

.ONESHELL:
dist-%: $$(call need_dist,$$*)
	@current=$(*)
	set -e
	set -- $(DEPS_$*)
	if [ "$$5" = "-" ]; then mode=bootstrap; else mode=mrustc; fi
	[ -z "$(call built,$(IMAGE):$*-dist)" ] || { echo "  = $(IMAGE):$*-dist уже собран (FORCE=1 — пересобрать)"; exit 0; }
	docker build -f Dockerfile $(NO_CACHE_TEXT) --target dist \
		--build-arg RUNTIME_IMAGE=$$1			\
		--build-arg BUILD_IMAGE=$$2				\
		--build-arg CMAKE_IMAGE=$$3				\
		--build-arg PYTHON_IMAGE=$$4			\
		--build-arg MRUSTC_VERSION=$$5			\
		--build-arg MRUSTC_TARGET_VER=$${current%.*}	\
		--build-arg MRUSTC_JOBS=$(MRUSTC_JOBS)	\
		--build-arg BUILD_MODE=$$mode			\
		--build-arg NINJA_VERSION=$(NINJA_VERSION)	\
		--build-arg RUST_VERSION=$$current		\
		--build-arg APT_DEPS="$(APT_DEPS)"		\
		--tag $(IMAGE):$$current-dist			\
		--progress plain $(BUILD_CONTEXT)


# docker build - истоговый образ с установленным ПО (установка из дистрибутива)
.PHONY: build
build: build-$(VERSION)

.ONESHELL:
build-%: $$(call need_build,$$*)
	@current=$(*)
	set -e
	set -- $(DEPS_$*)
	if [ "$$5" = "-" ]; then mode=bootstrap; else mode=mrustc; fi
	[ -z "$(call built,$(IMAGE):$*)" ] || { echo "  = $(IMAGE):$* уже собран (FORCE=1 — пересобрать)"; exit 0; }
	docker build -f Dockerfile $(NO_CACHE_TEXT) \
		--build-arg RUNTIME_IMAGE=$$1			\
		--build-arg BUILD_IMAGE=$$2				\
		--build-arg CMAKE_IMAGE=$$3				\
		--build-arg PYTHON_IMAGE=$$4			\
		--build-arg MRUSTC_VERSION=$$5			\
		--build-arg MRUSTC_TARGET_VER=$${current%.*}	\
		--build-arg MRUSTC_JOBS=$(MRUSTC_JOBS)	\
		--build-arg BUILD_MODE=$$mode			\
		--build-arg NINJA_VERSION=$(NINJA_VERSION)	\
		--build-arg RUST_VERSION=$$current		\
		--build-arg APT_DEPS="$(APT_DEPS)"		\
		--tag $(IMAGE):$$current				\
		--progress plain $(BUILD_CONTEXT)


# тестирование собранного образа
.PHONY: test
test: test-$(VERSION)

.ONESHELL:
test-%: build-%
	@current=$(*)
	docker run --rm --entrypoint /bin/bash -v $(TEST_DIR):/test:ro $(IMAGE):$$current -euc '\
		echo "== versions ==";	\
		rustc --version; 		\
		rustc --print sysroot; 	\
		cargo --version; 		\
		echo "== compile hello.rs (rustc) =="; 					\
		rustc -O /test/hello.rs -o /tmp/hello && /tmp/hello; 	\
		echo "== cargo new + run =="; 							\
		cp -r /test/demo /tmp/demo && cd /tmp/demo && cargo run --release --offline 2>&1 | tail -4'


.PHONY: clean
clean: clean-$(VERSION)
	rm -rf "$(LINKED_ARTIFACTS)"

.ONESHELL:
clean-%:
	@current=$(*)
	set -e
	set -- $(DEPS_$*)
	mkdir -p $(ARTIFACTS)/src
	[ "$$5" = "-" ] || rm -f "$(ARTIFACTS)/src/mrustc-$$5.tar.gz"
	rm -f "$(ARTIFACTS)/src/rustc-$$current-src.tar.gz"


.PHONY: all
all: sources dist build test
