BASE_DIR      	?= $(abspath .)
ARTIFACTS     	?= $(abspath $(BASE_DIR)/../../ci-artifacts/nodejs)
BUILD_CONTEXT 	?= $(BASE_DIR)
# рабочая копия $(ARTIFACTS) внутри контекста сборки (хардлинки, см. artifacts-link)
LINKED_ARTIFACTS ?= $(BUILD_CONTEXT)/artifacts
TEST_DIR		?= $(BASE_DIR)/test


.DEFAULT_GOAL := help

# список версий, которые поддерживает сборщик
# версии собираются одна за другой, строго по цепочке
VERSION_CHAIN		?= 8.17.0 10.24.1 12.22.12 14.21.3 16.20.2 18.20.5 20.18.1 22.11.0 22.22.1
# целевая версия сборки. по умолчанию берется последняя версия
# соответственно для ее сборки потребуется собрать все предыдущие версии в цепочки
VERSION      		?= $(lastword $(VERSION_CHAIN))
# базовый url адрес для скачивания исходников собираемых в Dockerfile
BASE_URL			?= https://nodejs.org/dist
# базовый url адрес для скачивания пакета pnpm из npm-реестра
PNPM_URL			?= https://registry.npmjs.org/pnpm/-
# npm-реестр и образ с npm для вендоринга vite (используется только на этапе sources).
# NPM_IMAGE — любой уже собранный образ этого же проекта; кэш от версии node не зависит
NPM_REGISTRY		?= https://registry.npmjs.org/
NPM_IMAGE			?= $(IMAGE):22
# базовый url для wasm-сборки rollup (версия определяется по дереву vite, см. sources)
ROLLUP_WASM_URL		?= https://registry.npmjs.org/@rollup/wasm-node/-
# выполнять ли сброс кэша: docker build --no-cache
NO_CACHE			?= 0

GLIBC_IMAGE			?= dmp/glibc:2.28
PYTHON_IMAGE		?= dmp/python:3.10-dist
GCC_IMAGE			?= dmp/gcc:12.4.0-dist
GCC10_IMAGE			?= dmp/gcc:10.5.0-dist
LLVM_IMAGE			?= dmp/llvm:20.1.8-dist
CMAKE_IMAGE			?= dmp/cmake:3.27.9-dist

# зависимости для сборки разных версии
# cc:   clang | gcc  — старый V8 (node <=16) не собирается clang 20, ему нужен gcc
# py2:  0 | 1        — node 8/10/12 требуют python2 для configure/gyp
# vite: версия | -   — ставится глобально рядом с npm/pnpm; «-» = не ставить.
#       vite 6 требует node ^18||^20||>=22, vite 7/8 — node ^20.19||>=22.12,
#       поэтому 22.11.0 и ниже получают vite 6, а 22.22.1 — vite 7
# версия           runtime_image	build_image		python_image	gcc_image		llvm_image		cc		py2	pnpm		vite
DEPS_8.17.0		:= $(GLIBC_IMAGE)	$(GLIBC_IMAGE)	$(PYTHON_IMAGE)	$(GCC10_IMAGE)	$(LLVM_IMAGE)	gcc		1	3.8.1		-
DEPS_10.24.1	:= $(GLIBC_IMAGE)	$(GLIBC_IMAGE)	$(PYTHON_IMAGE)	$(GCC10_IMAGE)	$(LLVM_IMAGE)	gcc		1	5.18.10		-
DEPS_12.22.12	:= $(GLIBC_IMAGE)	$(GLIBC_IMAGE)	$(PYTHON_IMAGE)	$(GCC10_IMAGE)	$(LLVM_IMAGE)	gcc		1	6.35.1		-
DEPS_14.21.3	:= $(GLIBC_IMAGE)	$(GLIBC_IMAGE)	$(PYTHON_IMAGE)	$(GCC10_IMAGE)	$(LLVM_IMAGE)	gcc		0	7.33.7		-
DEPS_16.20.2	:= $(GLIBC_IMAGE)	$(GLIBC_IMAGE)	$(PYTHON_IMAGE)	$(GCC10_IMAGE)	$(LLVM_IMAGE)	gcc		0	8.15.9		-
DEPS_18.20.5	:= $(GLIBC_IMAGE)	$(GLIBC_IMAGE)	$(PYTHON_IMAGE)	$(GCC_IMAGE)	$(LLVM_IMAGE)	clang	0	9.15.0		6.4.3
DEPS_20.18.1	:= $(GLIBC_IMAGE)	$(GLIBC_IMAGE)	$(PYTHON_IMAGE)	$(GCC_IMAGE)	$(LLVM_IMAGE)	clang	0	9.15.0		6.4.3
DEPS_22.11.0	:= $(GLIBC_IMAGE)	$(GLIBC_IMAGE)	$(PYTHON_IMAGE)	$(GCC_IMAGE)	$(LLVM_IMAGE)	clang	0	9.15.0		6.4.3
DEPS_22.22.1	:= $(GLIBC_IMAGE)	$(GLIBC_IMAGE)	$(PYTHON_IMAGE)	$(GCC_IMAGE)	$(LLVM_IMAGE)	clang	0	9.15.0		7.3.6

# базовое название для образа
IMAGE			:= dmp/nodejs
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
	@echo "  make test       проверить образ $(IMAGE): node, npm, npx, pnpm, vite, запуск скриптов"
	@echo ""
	@echo "  Цепочка: $(VERSION_CHAIN)"
	@echo "  node 18+ собирается clang-ом (dmp/llvm), node <=16 — gcc (dmp/gcc); рантайм — dmp/glibc."
	@echo "  Каждый образ тегируется полной и короткой версией (напр. 22.11.0 и 22)."
	@echo "  Зависимости (runtime_image/build_image/python_image/gcc_image/llvm_image/cc/py2/pnpm/vite) задаются пословно в DEPS_<версия>."
	@echo "  vite ставится глобально рядом с npm/pnpm (vite=- — не ставить); его зависимости"
	@echo "  вендорятся на make sources в $(ARTIFACTS)/src/vite/ образом NPM_IMAGE=$(NPM_IMAGE)."
	@echo "  rollup 4 в нем подменяется wasm-сборкой: нативный биндинг требует glibc >= 2.29."
	@echo "  Другая версия: make all VERSION=22.11.0"
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
	$(info PNPM_URL       = $(PNPM_URL))
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


# Скачиваем зависимости для сборки дистрибутива
.PHONY: sources
sources: sources-$(VERSION)

.ONESHELL:
sources-%:
	@current=$(*)
	set -e
	set -- $(DEPS_$*)
	mkdir -p $(ARTIFACTS)/src
	[ -f "$(ARTIFACTS)/src/node-v$$current.tar.xz" ] || curl -v -o "$(ARTIFACTS)/src/node-v$$current.tar.xz" "$(BASE_URL)/v$$current/node-v$$current.tar.xz"
	[ -f "$(ARTIFACTS)/src/pnpm-$$8.tgz" ] || curl -v -o "$(ARTIFACTS)/src/pnpm-$$8.tgz" "$(PNPM_URL)/pnpm-$$8.tgz"
	# vite тянет дерево зависимостей, поэтому вендорится не тарболом, а npm-кэшем:
	# docker-сборка потом ставит его с npm install -g --offline.
	# каталог создается всегда — Dockerfile копирует его безусловно
	vite_dir="$(ARTIFACTS)/src/vite"
	mkdir -p "$$vite_dir"
	touch "$$vite_dir/.keep"
	vite="$$9"
	if [ "$$vite" != "-" ] && [ ! -f "$$vite_dir/vite-cache-$$vite.tar.gz" ]; then
		command -v docker >/dev/null || { echo "  ! нужен docker: кэш vite наполняется в $(NPM_IMAGE)" >&2; exit 1; }
		docker image inspect $(NPM_IMAGE) >/dev/null 2>&1 || {
			echo "  ! нет образа $(NPM_IMAGE) — соберите любое звено с npm и укажите NPM_IMAGE=<образ>" >&2
			exit 1
		}
		tmp=$$(mktemp -d)
		trap 'rm -rf "$$tmp"' EXIT
		# npmrc для Nexus подключается как ~/.npmrc; без него — прямой реестр.
		# --user: иначе кэш создается от root
		if [ -f "$(ARTIFACTS)/npmrc" ]; then
			reg="-v $(ARTIFACTS)/npmrc:/tmp/.npmrc:ro"
		else
			reg="-e npm_config_registry=$(NPM_REGISTRY)"
		fi
		docker run --rm								\
			--user "$$(id -u):$$(id -g)"			\
			-v "$$tmp:/w" -w /w						\
			-e HOME=/tmp							\
			-e npm_config_cache=/w/cache			\
			$$reg									\
			--entrypoint npm						\
			$(NPM_IMAGE) install -g --prefix /w/prefix "vite@$$vite" --no-audit --no-fund
		tar -czf "$$vite_dir/vite-cache-$$vite.tar.gz" -C "$$tmp" cache
		du -h "$$vite_dir/vite-cache-$$vite.tar.gz"
		# rollup 4 (vite 5+) имеет только нативный биндинг, которому нужен glibc >= 2.29,
		# а рантайм — 2.28. качаем его же wasm-сборку, Dockerfile ей подменит rollup.
		# vite 4 тянет rollup 3 (чистый JS) — там подмена не нужна
		rollup_version=$$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))["version"])' \
			"$$tmp/prefix/lib/node_modules/vite/node_modules/rollup/package.json")
		echo "  vite $$vite -> rollup $$rollup_version"
		case "$$rollup_version" in
			[123].*)	echo "  rollup $$rollup_version без нативных биндингов — wasm не нужен" ;;
			*)			curl -fvL -o "$$vite_dir/rollup-wasm-$$vite.tgz" "$(ROLLUP_WASM_URL)/wasm-node-$$rollup_version.tgz" ;;
		esac
	fi


# docker build --target dist - дистрибутив с скомпилированными исходниками (без установки в систему)
.PHONY: dist
dist: dist-$(VERSION)

.ONESHELL:
dist-%: $$(call need_dist,$$*)
	@current=$(*)
	set -e
	set -- $(DEPS_$*)
	major=$${current%%.*}
	[ -z "$(call built,$(IMAGE):$*-dist)" ] || { echo "  = $(IMAGE):$*-dist уже собран (FORCE=1 — пересобрать)"; exit 0; }
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
		--build-arg VITE_VERSION=$$9 		\
		--tag $(IMAGE):$$current-dist 		\
		--tag $(IMAGE):$$major-dist 		\
		--progress plain $(BUILD_CONTEXT)


# docker build - истоговый образ с установленным ПО (установка из дистрибутива)
.PHONY: build
build: build-$(VERSION)

.ONESHELL:
build-%: $$(call need_build,$$*)
	@current=$(*)
	set -e
	set -- $(DEPS_$*)
	major=$${current%%.*}
	[ -z "$(call built,$(IMAGE):$*)" ] || { echo "  = $(IMAGE):$* уже собран (FORCE=1 — пересобрать)"; exit 0; }
	docker build -f Dockerfile $(NO_CACHE_TEXT) \
		--build-arg DIST_IMAGE=$(IMAGE):$*-dist	\
		--build-arg RUNTIME_IMAGE=$$1			\
		--build-arg BUILD_IMAGE=$$2				\
		--build-arg PYTHON_IMAGE=$$3			\
		--build-arg GCC_IMAGE=$$4				\
		--build-arg LLVM_IMAGE=$$5				\
		--build-arg NODE_VERSION=$$current		\
		--build-arg NODE_CC=$$6					\
		--build-arg NODE_PY2=$$7				\
		--build-arg PNPM_VERSION=$$8			\
		--build-arg VITE_VERSION=$$9			\
		--tag $(IMAGE):$$current				\
		--tag $(IMAGE):$$major					\
		--progress plain $(BUILD_CONTEXT)


# тестирование собранного образа
.PHONY: test
test: test-$(VERSION)

.ONESHELL:
test-%: build-%
	@current=$(*)
	set -- $(DEPS_$*)
	docker run --rm --entrypoint /bin/bash -v $(TEST_DIR):/test:ro -e VITE_VERSION="$$9" $(IMAGE):$$current -euc '\
		echo "== versions =="; 			\
		node --version; 				\
		npm --version; 					\
		npx --version; 					\
		pnpm --version; 				\
		if [ "$$VITE_VERSION" = "-" ]; then \
			echo "vite: не ставится для этой версии node"; \
		else 							\
			vite --version; 			\
			echo "== vite build (офлайн, без node_modules проекта) =="; \
			mkdir -p /tmp/vdemo/src && cp -r /test/vitedemo/. /tmp/vdemo/ && cd /tmp/vdemo \
			&& vite build 				\
			&& test -f dist/index.html 	\
			&& ls dist/assets 			\
			&& echo "  ok    статический бандл собран"; \
			cd /; 						\
		fi; 							\
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
	rm -rf "$(LINKED_ARTIFACTS)"

.ONESHELL:
clean-%:
	@current=$(*)
	set -e
	set -- $(DEPS_$*)
	rm -f "$(ARTIFACTS)/src/node-v$$current.tar.xz"
	rm -f "$(ARTIFACTS)/src/pnpm-$$8.tgz"
	[ "$$9" = "-" ] || rm -f "$(ARTIFACTS)/src/vite/vite-cache-$$9.tar.gz" "$(ARTIFACTS)/src/vite/rollup-wasm-$$9.tgz"


.PHONY: all
all: sources dist build test
