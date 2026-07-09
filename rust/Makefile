BASE_DIR      	?= $(abspath .)
ARTIFACTS     	?= $(BASE_DIR)/artifacts
BUILD_CONTEXT 	?= $(BASE_DIR)
TEST_DIR		?= $(BASE_DIR)/test


.DEFAULT_GOAL := help

# список версий, которые поддерживает сборщик
# версии собираются одна за другой, строго по цепочке
VERSION_CHAIN		?= 1.90.0
# целевая версия сборки. по умолчанию берется последняя версия
# соответственно для ее сборки потребуется собрать все предыдущие версии в цепочки
VERSION      		?= $(lastword $(VERSION_CHAIN))
# базовый url адрес для скачивания исходников собираемых в Dockerfile
RUST_SRC_BASE		?= https://static.rust-lang.org/dist
MRUSTC_SRC_BASE		?= https://github.com/thepowersgang/mrustc/archive/refs/tags
APT_DEPS			?= patch curl git zlib1g-dev libssl-dev pkg-config
# выполнять ли сброс кэша: docker build --no-cache
NO_CACHE			?= 0

GCC_IMAGE			?= dmp/gcc:8.5.0
CMAKE_IMAGE			?= dmp/cmake:3.27.9-dist
PYTHON_IMAGE		?= dmp/python:3.10-dist

# зависимости для сборки разных версии
# версия       runtime_image   	build_image     cmake_image     python_image    mrustc
DEPS_1.90.0	:= $(GCC_IMAGE)		$(GCC_IMAGE)	$(CMAKE_IMAGE)	$(PYTHON_IMAGE)	0.12.0

# базовое название для образа
IMAGE			:= dmp/rust
FIRST_VERSION	:= $(firstword $(VERSION_CHAIN))
NO_CACHE_TEXT	:= $(if $(filter 1,$(NO_CACHE)),--no-cache,)


# helper: поиск предыдущей версии в VERSION_CHAIN
prev_of = $(shell echo "$(VERSION_CHAIN)" | awk -v v='$(1)' '{for(i=1;i<=NF;i++){if($$i==v){print p;exit};p=$$i}}')


.PHONY: help
help:
	@echo "make:"
	@echo "  make all        собрать всё"
	@echo "  make sources    скачать исходники для цепочки вплоть до $(VERSION)"
	@echo "  make dist       собрать цепочку -dist образов одну за другой до $(VERSION)"
	@echo "  make build      собрать образ $(IMAGE)"
	@echo "  make test       проверить тулчейн: собрать и запустить test/hello.rs в образе $(IMAGE)"
	@echo ""
	@echo "  Цепочка: $(VERSION_CHAIN)"
	@echo "  rustc собирается ИЗ ИСХОДНИКОВ через mrustc (C++), без бинарного rustc."
	@echo "  Зависимости (cmake/python/mrustc) задаются пословно в DEPS_<версия>."
	@echo "  Другая версия: make all VERSION=1.90.0"
	@echo ""


.PHONY: debug
debug:
	$(info BASE_DIR       = $(BASE_DIR))
	$(info ARTIFACTS      = $(ARTIFACTS))
	$(info BUILD_CONTEXT  = $(BUILD_CONTEXT))
	$(info TEST_DIR       = $(TEST_DIR))
	$(info VERSION_CHAIN  = $(VERSION_CHAIN))
	$(info VERSION        = $(VERSION))
	$(info FIRST_VERSION  = $(FIRST_VERSION))
	$(info RUST_SRC_BASE  = $(RUST_SRC_BASE))
	$(info MRUSTC_SRC_BASE = $(MRUSTC_SRC_BASE))
	$(info APT_DEPS       = $(APT_DEPS))
	$(info NO_CACHE       = $(NO_CACHE))
	$(info NO_CACHE_TEXT  = $(NO_CACHE_TEXT))
	$(info IMAGE          = $(IMAGE):$(VERSION))
	$(info DEPS($(VERSION)) = $(DEPS_$(VERSION)))
	@true


# Скачиваем зависимости для сборки дистрибутива
.PHONY: sources
sources: sources-$(VERSION)

.ONESHELL:
sources-%:
	@current=$(*)
	@prev='$(call prev_of,$*)'
	# вызываем рекурсивно предыдущую версию
	[ -n "$$prev" ] && $(MAKE) sources-$$prev || true
	set -e
	set -- $(DEPS_$*)
	mkdir -p $(ARTIFACTS)/src
	[ -f "$(ARTIFACTS)/src/mrustc-$$5.tar.gz" ] || curl -v -o "$(ARTIFACTS)/src/mrustc-$$5.tar.gz" "$(MRUSTC_SRC_BASE)/v$$5.tar.gz"
	[ -f "$(ARTIFACTS)/src/rustc-$$current-src.tar.gz" ] || curl -v -o "$(ARTIFACTS)/src/rustc-$$current-src.tar.gz" "$(RUST_SRC_BASE)/rustc-$$current-src.tar.gz"


# docker build --target dist - дистрибутив с скомпилированными исходниками (без установки в систему)
.PHONY: dist
dist: dist-$(VERSION)

.ONESHELL:
dist-%: sources-%
	@current=$(*)
	prev='$(call prev_of,$*)'
	# рекурсивно собираем предыдущую версию
	[ -n "$$prev" ] && $(MAKE) dist-$$prev || true
	set -e
	set -- $(DEPS_$*)
	docker build -f Dockerfile $(NO_CACHE_TEXT) --target dist \
		--build-arg RUNTIME_IMAGE=$$1			\
		--build-arg BUILD_IMAGE=$$2				\
		--build-arg CMAKE_IMAGE=$$3				\
		--build-arg PYTHON_IMAGE=$$4			\
		--build-arg MRUSTC_VERSION=$$5			\
		--build-arg RUST_VERSION=$$current		\
		--build-arg APT_DEPS="$(APT_DEPS)"		\
		--tag $(IMAGE):$$current-dist			\
		--progress plain $(BUILD_CONTEXT)


# docker build - истоговый образ с установленным ПО (установка из дистрибутива)
.PHONY: build
build: build-$(VERSION)

.ONESHELL:
build-%: dist-%
	@current=$(*)
	prev='$(call prev_of,$*)'
	# рекурсивно собираем предыдущую версию
	[ -n "$$prev" ] && $(MAKE) build-$$prev || true
	set -e
	set -- $(DEPS_$*)
	docker build -f Dockerfile $(NO_CACHE_TEXT) \
		--build-arg RUNTIME_IMAGE=$$1			\
		--build-arg BUILD_IMAGE=$$2				\
		--build-arg CMAKE_IMAGE=$$3				\
		--build-arg PYTHON_IMAGE=$$4			\
		--build-arg MRUSTC_VERSION=$$5			\
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

.ONESHELL:
clean-%:
	@current=$(*)
	@prev='$(call prev_of,$*)'
	# вызываем рекурсивно предыдущую версию
	[ -n "$$prev" ] && $(MAKE) clean-$$prev || true
	set -e
	set -- $(DEPS_$*)
	mkdir -p $(ARTIFACTS)/src
	rm -f "$(ARTIFACTS)/src/mrustc-$$5.tar.gz" "$(ARTIFACTS)/src/rustc-$$current-src.tar.gz"


.PHONY: all
all: sources dist build test
