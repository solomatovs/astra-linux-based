BASE_DIR      	?= $(abspath .)
ARTIFACTS     	?= $(abspath $(BASE_DIR)/../../ci-artifacts/chsimcity)
BUILD_CONTEXT 	?= $(BASE_DIR)
# рабочая копия $(ARTIFACTS) внутри контекста сборки (хардлинки, см. artifacts-link)
LINKED_ARTIFACTS ?= $(BUILD_CONTEXT)/artifacts
TEST_DIR		?= $(BASE_DIR)/test

# путь, по которому дистрибутив лежит внутри -dist образа (SITE_PATH в Dockerfile)
SITE_PATH		?= /opt/chsimcity


.DEFAULT_GOAL := help

# список версий, которые поддерживает сборщик
# тегов и релизов в апстриме нет: «версия» — это version из package.json,
# а сам код фиксируется коммитом в COMMIT_<версия>. при обновлении апстрима
# добавляйте новое звено (версию из package.json + новый коммит)
VERSION_CHAIN		?= 0.1.0
COMMIT_0.1.0		:= 8f9648cd6f5c461a966d9e820509faa1d77950ce
# целевая версия сборки. по умолчанию берется последняя версия
VERSION      		?= $(lastword $(VERSION_CHAIN))
# базовый url адрес для скачивания исходников собираемых в Dockerfile
BASE_URL			?= https://codeload.github.com/Slach/CHSimCity/tar.gz
# npm-реестр, используется только на этапе sources (вендоринг npm-кэша);
# если в $(ARTIFACTS)/npmrc лежит конфиг для Nexus — берется он (см. npmrc.example)
NPM_REGISTRY		?= https://registry.npmjs.org/
# выполнять ли сброс кэша: docker build --no-cache
NO_CACHE			?= 0

NODE_IMAGE			?= dmp/nodejs:22
NGINX_IMAGE			?= dmp/nginx:1.28
# базовый url для wasm-сборки rollup (см. sources: версия берется из package-lock)
ROLLUP_WASM_URL		?= https://registry.npmjs.org/@rollup/wasm-node/-

# зависимости для сборки разных версии
# апстрим требует node >= 20 (vite 7)
# версия           runtime_image	node_image
DEPS_0.1.0		:= $(NGINX_IMAGE)	$(NODE_IMAGE)

# базовое название для образа
IMAGE			:= dmp/chsimcity
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
	@echo "make:"
	@echo "  make all        собрать всё"
	@echo "  make sources    скачать исходники и свендорить npm-кэш для $(VERSION)"
	@echo "  make dist       собрать -dist образ $(VERSION) (статический бандл)"
	@echo "  make extract    достать бандл ($(SITE_PATH)) из -dist образа в $(ARTIFACTS)/build/<версия>"
	@echo "  make build      собрать образ $(IMAGE) (nginx + бандл)"
	@echo "  make test       проверить образ $(IMAGE): файлы, nginx, /healthz, Content-Type"
	@echo ""
	@echo "  Цепочка: $(VERSION_CHAIN)"
	@echo "  CHSimCity — обучающая 3D-модель ClickHouse (three.js/vite),"
	@echo "  собирается в статический бандл и раздается nginx'ом."
	@echo "  Тегов в апстриме нет: версия = version из package.json,"
	@echo "  код фиксируется коммитом в COMMIT_<версия>."
	@echo "  npm-зависимости вендорятся на этапе make sources в"
	@echo "  $(ARTIFACTS)/src/npm-cache-<версия>.tar.gz, сборка идет с npm ci --offline."
	@echo "  Нативный биндинг rollup 4 требует glibc >= 2.29 (база — 2.28), поэтому"
	@echo "  при сборке rollup подменяется своей же wasm-сборкой @rollup/wasm-node."
	@echo "  Закрытый контур: положите npmrc для Nexus в $(ARTIFACTS)/npmrc (см. npmrc.example)."
	@echo ""
	@echo "  make <стадия>-all  то же для всех версий из VERSION_CHAIN"
	@echo "  FORCE=1            пересобрать, даже если образ уже есть"


.PHONY: debug
debug:
	$(info BASE_DIR       = $(BASE_DIR))
	$(info ARTIFACTS      = $(ARTIFACTS))
	$(info SITE_PATH      = $(SITE_PATH))
	$(info BUILD_CONTEXT  = $(BUILD_CONTEXT))
	$(info LINKED_ARTIFACTS = $(LINKED_ARTIFACTS))
	$(info TEST_DIR       = $(TEST_DIR))
	$(info VERSION_CHAIN  = $(VERSION_CHAIN))
	$(info VERSION        = $(VERSION))
	$(info COMMIT         = $(COMMIT_$(VERSION)))
	$(info FIRST_VERSION  = $(FIRST_VERSION))
	$(info BASE_URL       = $(BASE_URL))
	$(info NPM_REGISTRY   = $(NPM_REGISTRY))
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
	commit='$(COMMIT_$*)'
	[ -n "$$commit" ] || { echo "  ! COMMIT_$$current не задан в Makefile" >&2; exit 1; }
	mkdir -p $(ARTIFACTS)/src
	src="$(ARTIFACTS)/src/chsimcity-$$current-src.tar.gz"
	[ -f "$$src" ] || curl -fvL -o "$$src" "$(BASE_URL)/$$commit"
	# wasm-сборка rollup: нативный биндинг rollup 4 требует glibc >= 2.29, а база — 2.28.
	# версия должна совпадать с rollup из package-lock, иначе vite получит чужой API
	wasm="$(ARTIFACTS)/src/rollup-wasm-$$current.tgz"
	if [ ! -f "$$wasm" ]; then
		rollup_version=$$(tar -xzOf "$$src" --wildcards '*/package-lock.json' \
			| python3 -c 'import json,sys; print(json.load(sys.stdin)["packages"]["node_modules/rollup"]["version"])')
		echo "  rollup из package-lock: $$rollup_version"
		curl -fvL -o "$$wasm" "$(ROLLUP_WASM_URL)/wasm-node-$$rollup_version.tgz"
	fi
	# npm-кэш — единственное, что сборка тянула бы из сети. наполняем его заранее
	# тем же node-образом, которым потом собираем: npm ci по package-lock.json
	cache="$(ARTIFACTS)/src/npm-cache-$$current.tar.gz"
	if [ ! -f "$$cache" ]; then
		tmp=$$(mktemp -d)
		trap 'rm -rf "$$tmp"' EXIT
		tar -xzf "$$src" -C "$$tmp" --strip-components=1
		# npmrc для Nexus подключается как ~/.npmrc; без него — прямой реестр.
		# --user: иначе кэш создается от root
		if [ -f "$(ARTIFACTS)/npmrc" ]; then
			reg="-v $(ARTIFACTS)/npmrc:/tmp/.npmrc:ro"
		else
			reg="-e npm_config_registry=$(NPM_REGISTRY)"
		fi
		docker run --rm								\
			--user "$$(id -u):$$(id -g)"			\
			-v "$$tmp:/src" -w /src					\
			-e HOME=/tmp							\
			-e npm_config_cache=/src/.npm-cache		\
			$$reg									\
			--entrypoint npm						\
			$$2 ci --no-audit --no-fund
		tar -czf "$$cache" -C "$$tmp" .npm-cache
		du -h "$$cache"
	fi


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
		--build-arg RUNTIME_IMAGE=$$1		\
		--build-arg NODE_IMAGE=$$2			\
		--build-arg SITE_VERSION=$$current	\
		--tag $(IMAGE):$$current-dist		\
		--progress plain $(BUILD_CONTEXT)


# извлечь собранный бандл ($(SITE_PATH)) из -dist образа в artifacts/build/<версия>:
# статику можно раздать и любым другим web-сервером
.PHONY: extract
extract: extract-$(VERSION)

.ONESHELL:
extract-%: dist-%
	@current=$(*)
	set -e
	img='$(IMAGE):'"$$current"'-dist'
	dst='$(ARTIFACTS)/build/'"$$current"
	# создаём временный контейнер из -dist образа (без запуска) и копируем из него дерево.
	# --entrypoint обязателен: у dist-стадии нет своих ENTRYPOINT/CMD
	cid=$$(docker create --entrypoint /bin/true $$img)
	trap 'docker rm -f $$cid >/dev/null 2>&1 || true' EXIT
	rm -rf "$$dst"
	mkdir -p "$$dst"
	docker cp "$$cid:$(SITE_PATH)/." "$$dst/"
	echo "extracted $$img:$(SITE_PATH) -> $$dst"
	du -h --max-depth=1 "$$dst" || true


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
		--build-arg NODE_IMAGE=$$2			\
		--build-arg SITE_VERSION=$$current	\
		--tag $(IMAGE):$$current			\
		--progress plain $(BUILD_CONTEXT)


# тестирование собранного образа
.PHONY: test
test: test-$(VERSION)

.ONESHELL:
test-%: build-%
	@current=$(*)
	docker run --rm --entrypoint /bin/bash -v $(TEST_DIR):/test:ro $(IMAGE):$$current /test/check.sh


.PHONY: clean
clean: clean-$(VERSION)
	rm -rf "$(LINKED_ARTIFACTS)"

.ONESHELL:
clean-%:
	@current=$(*)
	set -e
	rm -f "$(ARTIFACTS)/src/chsimcity-$$current-src.tar.gz"
	rm -f "$(ARTIFACTS)/src/npm-cache-$$current.tar.gz"
	rm -f "$(ARTIFACTS)/src/rollup-wasm-$$current.tgz"
	rm -rf "$(ARTIFACTS)/build/$$current"


.PHONY: all
all: sources dist build test
