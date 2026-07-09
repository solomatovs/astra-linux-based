BASE_DIR      	?= $(abspath .)
ARTIFACTS     	?= $(BASE_DIR)/artifacts
BUILD_CONTEXT 	?= $(BASE_DIR)
TEST_DIR		?= $(BASE_DIR)/test


.DEFAULT_GOAL := help

# список версий, которые поддерживает сборщик
# версии собираются одна за другой, строго по цепочке
VERSION_CHAIN		?= 3.8.20 3.9.25 3.10.20 3.11.15 3.12.13 3.13.14 3.14.6
# целевая версия сборки. по умолчанию берется последняя версия
# соответственно для ее сборки потребуется собрать все предыдущие версии в цепочки
VERSION      		?= $(lastword $(VERSION_CHAIN))
# базовый url адрес для скачивания исходников собираемых в Dockerfile
BASE_URL			?= https://www.python.org/ftp/python
# выполнять ли сброс кэша: docker build --no-cache
NO_CACHE			?= 0
# apt-зависимости для сборки python (одинаковы для всех версий)
APT_DEPS			?= zlib1g-dev libncurses5-dev libgdbm-dev libnss3-dev libssl-dev libreadline-dev libffi-dev libsqlite3-dev libbz2-dev liblzma-dev curl

GLIBC_IMAGE			?= dmp/glibc:2.28
GCC_IMAGE			?= dmp/gcc:8.5.0

# зависимости для сборки разных версии
# версия           runtime_image		build_image
DEPS_3.8.20		:= $(GLIBC_IMAGE)	$(GCC_IMAGE)
DEPS_3.9.25		:= $(GLIBC_IMAGE)	$(GCC_IMAGE)
DEPS_3.10.20	:= $(GLIBC_IMAGE)	$(GCC_IMAGE)
DEPS_3.11.15	:= $(GLIBC_IMAGE)	$(GCC_IMAGE)
DEPS_3.12.13	:= $(GLIBC_IMAGE)	$(GCC_IMAGE)
DEPS_3.13.14	:= $(GLIBC_IMAGE)	$(GCC_IMAGE)
DEPS_3.14.6		:= $(GLIBC_IMAGE)	$(GCC_IMAGE)

# базовое название для образа
IMAGE			:= dmp/python
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
	@echo "  make test       проверить образ: python, pip, C-модули, ssl, sqlite, ffi"
	@echo ""
	@echo "  Цепочка: $(VERSION_CHAIN)"
	@echo "  Каждая версия собирается тулчейном dmp/gcc, рантайм — dmp/glibc."
	@echo "  Каждый образ тегируется полной и короткой версией (напр. 3.12.13 и 3.12)."
	@echo "  Зависимости (apt-пакеты) задаются в переменной APT_DEPS."
	@echo "  Другая версия: make all VERSION=3.11.15"
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
	$(info BASE_URL       = $(BASE_URL))
	$(info NO_CACHE       = $(NO_CACHE))
	$(info NO_CACHE_TEXT  = $(NO_CACHE_TEXT))
	$(info APT_DEPS       = $(APT_DEPS))
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
	mkdir -p $(ARTIFACTS)/src
	[ -f "$(ARTIFACTS)/src/Python-$$current.tar.xz" ] || curl -v -O --output-dir "$(ARTIFACTS)/src" "$(BASE_URL)/$$current/Python-$$current.tar.xz"


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
	runtime=$$1; build=$$2
	short=$${current%.*}
	docker build -f Dockerfile $(NO_CACHE_TEXT) --target dist \
		--build-arg RUNTIME_IMAGE=$$runtime 	\
		--build-arg BUILD_IMAGE=$$build 		\
		--build-arg PYTHON_VERSION=$$current 	\
		--build-arg APT_DEPS="$(APT_DEPS)" 		\
		--tag $(IMAGE):$$current-dist 			\
		--tag $(IMAGE):$$short-dist 			\
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
	runtime=$$1; build=$$2
	short=$${current%.*}
	docker build -f Dockerfile $(NO_CACHE_TEXT) \
		--build-arg RUNTIME_IMAGE=$$runtime 	\
		--build-arg BUILD_IMAGE=$$build 		\
		--build-arg PYTHON_VERSION=$$current 	\
		--build-arg APT_DEPS="$(APT_DEPS)" 		\
		--tag $(IMAGE):$$current 				\
		--tag $(IMAGE):$$short 					\
		--progress plain $(BUILD_CONTEXT)


# тестирование собранного образа
.PHONY: test
test: test-$(VERSION)

.ONESHELL:
test-%: build-%
	@current=$(*)
	docker run --rm --entrypoint /bin/bash -v $(TEST_DIR):/test:ro $(IMAGE):$$current -euc '\
		echo "== interpreter & pip =="; 	\
		python3 --version; 					\
		python3 -c "import sys; print(sys.executable)"; \
		python3 -m pip --version; 			\
		python3 -m pip list 2>/dev/null | head -n 20; 	\
		echo "== functional checks =="; 	\
		python3 /test/pycheck.py'


.PHONY: clean
clean: clean-$(VERSION)

.ONESHELL:
clean-%:
	@current=$(*)
	@prev='$(call prev_of,$*)'
	# вызываем рекурсивно предыдущую версию
	[ -n "$$prev" ] && $(MAKE) clean-$$prev || true
	set -e
	rm -f "$(ARTIFACTS)/src/Python-$$current.tar.xz"


.PHONY: all
all: sources dist build test
