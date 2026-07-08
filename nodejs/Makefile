BASE_DIR      	?= $(abspath .)
ARTIFACTS     	?= $(BASE_DIR)/artifacts
BUILD_CONTEXT 	?= $(BASE_DIR)
TEST_DIR		?= $(BASE_DIR)/test


.DEFAULT_GOAL := help

# список версий, которые поддерживает сборщик
# версии собираются одна за другой, строго по цепочке
VERSION_CHAIN		?= 8.17.0 10.24.1 12.22.12 14.21.3 16.20.2 18.20.5 20.18.1 22.11.0
# целевая версия сборки. по умолчанию берется последняя версия
# соответственно для ее сборки потребуется собрать все предыдущие версии в цепочки
VERSION      		?= $(lastword $(VERSION_CHAIN))
# базовый url адрес для скачивания исходников собираемых в Dockerfile
BASE_URL			?= https://nodejs.org/dist
# базовый url адрес для скачивания пакета pnpm из npm-реестра
PNPM_URL			?= https://registry.npmjs.org/pnpm/-
# выполнять ли сброс кэша: docker build --no-cache
NO_CACHE			?= 0

GLIBC_IMAGE			?= dmp/glibc:2.28
PYTHON_IMAGE		?= dmp/python:3.10-dist
GCC_IMAGE			?= dmp/gcc:12.4.0-dist
GCC10_IMAGE			?= dmp/gcc:10.5.0-dist
LLVM_IMAGE			?= dmp/llvm:20.1.8-dist
CMAKE_IMAGE			?= dmp/cmake:3.27.9-dist

# зависимости для сборки разных версии
# cc:  clang | gcc  — старый V8 (node <=16) не собирается clang 20, ему нужен gcc
# py2: 0 | 1        — node 8/10/12 требуют python2 для configure/gyp
# версия           runtime_image	build_image		python_image	gcc_image		llvm_image		cc		py2	pnpm
DEPS_8.17.0		:= $(GLIBC_IMAGE)	$(GLIBC_IMAGE)	$(PYTHON_IMAGE)	$(GCC10_IMAGE)	$(LLVM_IMAGE)	gcc		1	3.8.1
DEPS_10.24.1	:= $(GLIBC_IMAGE)	$(GLIBC_IMAGE)	$(PYTHON_IMAGE)	$(GCC10_IMAGE)	$(LLVM_IMAGE)	gcc		1	5.18.10
DEPS_12.22.12	:= $(GLIBC_IMAGE)	$(GLIBC_IMAGE)	$(PYTHON_IMAGE)	$(GCC10_IMAGE)	$(LLVM_IMAGE)	gcc		1	6.35.1
DEPS_14.21.3	:= $(GLIBC_IMAGE)	$(GLIBC_IMAGE)	$(PYTHON_IMAGE)	$(GCC10_IMAGE)	$(LLVM_IMAGE)	gcc		0	7.33.7
DEPS_16.20.2	:= $(GLIBC_IMAGE)	$(GLIBC_IMAGE)	$(PYTHON_IMAGE)	$(GCC10_IMAGE)	$(LLVM_IMAGE)	gcc		0	8.15.9
DEPS_18.20.5	:= $(GLIBC_IMAGE)	$(GLIBC_IMAGE)	$(PYTHON_IMAGE)	$(GCC_IMAGE)	$(LLVM_IMAGE)	clang	0	9.15.0
DEPS_20.18.1	:= $(GLIBC_IMAGE)	$(GLIBC_IMAGE)	$(PYTHON_IMAGE)	$(GCC_IMAGE)	$(LLVM_IMAGE)	clang	0	9.15.0
DEPS_22.11.0	:= $(GLIBC_IMAGE)	$(GLIBC_IMAGE)	$(PYTHON_IMAGE)	$(GCC_IMAGE)	$(LLVM_IMAGE)	clang	0	9.15.0

# базовое название для образа
IMAGE			:= dmp/nodejs
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
	@echo "  make test       проверить образ $(IMAGE): node, npm, npx, pnpm, запуск скриптов"
	@echo ""
	@echo "  Цепочка: $(VERSION_CHAIN)"
	@echo "  node 18+ собирается clang-ом (dmp/llvm), node <=16 — gcc (dmp/gcc); рантайм — dmp/glibc."
	@echo "  Каждый образ тегируется полной и короткой версией (напр. 22.11.0 и 22)."
	@echo "  Зависимости (runtime_image/build_image/python_image/gcc_image/llvm_image/cc/py2/pnpm) задаются пословно в DEPS_<версия>."
	@echo "  Другая версия: make all VERSION=22.11.0"
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
	$(info PNPM_URL       = $(PNPM_URL))
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
	curl -v --skip-existing -O --output-dir "$(ARTIFACTS)/src" "$(BASE_URL)/v$$current/node-v$$current.tar.xz"
	curl -v --skip-existing -O --output-dir "$(ARTIFACTS)/src" "$(PNPM_URL)/pnpm-$$8.tgz"


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
	major=$${current%%.*}
	docker build -f Dockerfile $(NO_CACHE_TEXT) --target dist \
		--build-arg RUNTIME_IMAGE=$$1		\
		--build-arg BUILD_IMAGE=$$2 		\
		--build-arg PYTHON_IMAGE=$$3 		\
		--build-arg GCC_IMAGE=$$4 			\
		--build-arg LLVM_IMAGE=$$5 			\
		--build-arg NODE_VERSION=$$current 	\
		--build-arg NODE_CC=$$6 			\
		--build-arg NODE_PY2=$$7 			\
		--build-arg PNPM_VERSION=$$8 		\
		--tag $(IMAGE):$$current-dist 		\
		--tag $(IMAGE):$$major-dist 		\
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
	major=$${current%%.*}
	docker build -f Dockerfile $(NO_CACHE_TEXT) \
		--build-arg RUNTIME_IMAGE=$$1			\
		--build-arg BUILD_IMAGE=$$2				\
		--build-arg PYTHON_IMAGE=$$3			\
		--build-arg GCC_IMAGE=$$4				\
		--build-arg LLVM_IMAGE=$$5				\
		--build-arg NODE_VERSION=$$current		\
		--build-arg NODE_CC=$$6					\
		--build-arg NODE_PY2=$$7				\
		--build-arg PNPM_VERSION=$$8			\
		--tag $(IMAGE):$$current				\
		--tag $(IMAGE):$$major					\
		--progress plain $(BUILD_CONTEXT)


# тестирование собранного образа
.PHONY: test
test: test-$(VERSION)

.ONESHELL:
test-%: build-%
	@current=$(*)
	docker run --rm --entrypoint /bin/bash -v $(TEST_DIR):/test:ro $(IMAGE):$$current -euc '\
		echo "== versions =="; 			\
		node --version; 				\
		npm --version; 					\
		npx --version; 					\
		pnpm --version; 				\
		command -v corepack >/dev/null && corepack --version || echo "corepack: n/a"; \
		echo "== which =="; 			\
		command -v node; command -v npm; command -v npx; command -v pnpm; command -v pnpx; \
		echo "== run hello.js ==";		\
		node /test/hello.js; 			\
		echo "== functional checks =="; \
		node /test/nodecheck.js; 		\
		echo "== npm offline (init + local link) =="; \
		mkdir -p /tmp/demo && cp -r /test/demo/. /tmp/demo/ && cd /tmp/demo \
		&& npm --offline run start; 	\
		echo "== pnpm run (no deps) ==";\
		mkdir -p /tmp/pdemo && cp -r /test/demo/. /tmp/pdemo/ && cd /tmp/pdemo \
		&& pnpm start'


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
	rm -f "$(ARTIFACTS)/src/node-v$$current.tar.xz"
	rm -f "$(ARTIFACTS)/src/pnpm-$$8.tgz"


.PHONY: all
all: sources dist build test
