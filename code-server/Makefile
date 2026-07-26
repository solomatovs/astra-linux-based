BASE_DIR      	?= $(abspath .)
ARTIFACTS     	?= $(BASE_DIR)/artifacts
BUILD_CONTEXT 	?= $(BASE_DIR)
TEST_DIR		?= $(BASE_DIR)/test

# путь, по которому дистрибутив лежит внутри -dist образа (CS_PATH в Dockerfile)
CS_PATH			?= /opt/code-server


.DEFAULT_GOAL := help

# список версий, которые поддерживает сборщик
VERSION_CHAIN		?= 4.116.0 4.117.0
# целевая версия сборки. по умолчанию берется последняя версия
VERSION      		?= $(lastword $(VERSION_CHAIN))
# базовый url адрес для скачивания исходников собираемых в Dockerfile
BASE_URL			?= https://codeload.github.com/coder/code-server/tar.gz/refs/tags
# lib/vscode — git-сабмодуль, тарбол берется по запиненному коммиту
VSCODE_URL			?= https://codeload.github.com/microsoft/vscode/tar.gz
# ripgrep: @vscode/ripgrep тянет готовый бинарник с api.github.com
RIPGREP_URL			?= https://github.com/microsoft/ripgrep-prebuilt/releases/download
# заголовки electron: корневой .npmrc в vscode собирает нативные модули под electron
ELECTRON_URL		?= https://www.electronjs.org/headers
# встроенные расширения (js-debug и др.), перечислены в product.json
MARKETPLACE_URL		?= https://marketplace.visualstudio.com/_apis/public/gallery/publishers
# выполнять ли сброс кэша: docker build --no-cache
NO_CACHE			?= 0

# node 22.22.1 — версия из .nvmrc собираемого vscode. более старый node не проходит
# проверку в build/npm/preinstall.ts, поэтому 22.11.0 здесь не годится
NODE_IMAGE			?= dmp/nodejs:22.22.1
GCC_IMAGE			?= dmp/gcc:12.4.0-dist
PYTHON_IMAGE		?= dmp/python:3.10-dist

# зависимости для сборки разных версии
# vscode_sha:  коммит сабмодуля lib/vscode
# ripgrep:     тег prebuilt-архива из postinstall.js пакета @vscode/ripgrep
# electron:    target из lib/vscode/.npmrc
# node:        target из lib/vscode/remote/.npmrc, он же node внутри reh-бандла
# узнать значения для новой версии: make deps VERSION=<версия>
# версия           runtime_image	gcc_image		python_image	vscode_sha									ripgrep		ripgrep_target					electron	node
DEPS_4.116.0	:= $(NODE_IMAGE)	$(GCC_IMAGE)	$(PYTHON_IMAGE)	560a9dba96f961efea7b1612916f89e5d5d4d679	v15.0.1		x86_64-unknown-linux-musl		39.8.7		22.22.1
DEPS_4.117.0	:= $(NODE_IMAGE)	$(GCC_IMAGE)	$(PYTHON_IMAGE)	10c8e557c8b9f9ed0a87f61f1c9a44bde731c409	v15.0.1		x86_64-unknown-linux-musl		39.8.7		22.22.1

# базовое название для образа
IMAGE			:= dmp/code-server
FIRST_VERSION	:= $(firstword $(VERSION_CHAIN))
NO_CACHE_TEXT	:= $(if $(filter 1,$(NO_CACHE)),--no-cache,)


# helper: поиск предыдущей версии в VERSION_CHAIN
prev_of = $(shell echo "$(VERSION_CHAIN)" | awk -v v='$(1)' '{for(i=1;i<=NF;i++){if($$i==v){print p;exit};p=$$i}}')


.PHONY: help
help:
	@echo "make:"
	@echo "  make all        собрать всё"
	@echo "  make sources    скачать исходники для цепочки вплоть до $(VERSION)"
	@echo "  make deps       показать версии зависимостей, которые требуют исходники $(VERSION) (для переноса в DEPS)"
	@echo "  make dist       собрать цепочку -dist образов одну за другой до $(VERSION)"
	@echo "  make extract    достать дистрибутив ($(CS_PATH)) из -dist образа в $(ARTIFACTS)/build/<версия>"
	@echo "  make build      собрать образ $(IMAGE)"
	@echo "  make test       проверить образ $(IMAGE): версии, запуск сервера, HTTP-ответ"
	@echo ""
	@echo "  Цепочка: $(VERSION_CHAIN)"
	@echo "  Собирается из исходников по upstream ci/: code-server + сабмодуль lib/vscode,"
	@echo "  патчи из patches/ через quilt, затем gulp vscode-reh-web-linux-x64-min"
	@echo "  и build-release.sh."
	@echo "  Всё, что апстрим качает мимо npm-реестра (ripgrep, заголовки node и electron,"
	@echo "  node для reh-бандла, встроенные расширения), заранее лежит в $(ARTIFACTS)/src"
	@echo "  и подкладывается до запуска сборки — см. sources и Dockerfile."
	@echo "  Зависимости задаются пословно в DEPS_<версия>."
	@echo "  Другая версия: make all VERSION=4.116.0"
	@echo ""


.PHONY: debug
debug:
	$(info BASE_DIR       = $(BASE_DIR))
	$(info ARTIFACTS      = $(ARTIFACTS))
	$(info CS_PATH        = $(CS_PATH))
	$(info BUILD_CONTEXT  = $(BUILD_CONTEXT))
	$(info TEST_DIR       = $(TEST_DIR))
	$(info VERSION_CHAIN  = $(VERSION_CHAIN))
	$(info VERSION        = $(VERSION))
	$(info FIRST_VERSION  = $(FIRST_VERSION))
	$(info BASE_URL       = $(BASE_URL))
	$(info VSCODE_URL     = $(VSCODE_URL))
	$(info RIPGREP_URL    = $(RIPGREP_URL))
	$(info ELECTRON_URL   = $(ELECTRON_URL))
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
	for spec in $$v														\
		"$(BASE_URL)/v$$current|code-server-$$current-src.tar.gz"		\
		"$(VSCODE_URL)/$$4|vscode-$$4.tar.gz"							\
		"$(RIPGREP_URL)/$$5/ripgrep-$$5-$$6.tar.gz|"					\
		"$(ELECTRON_URL)/v$$7/node-v$$7-headers.tar.gz|electron-v$$7-headers.tar.gz" \
	; do url=$${spec%%|*}; name=$${spec#*|}; name=$${name:-$${url##*/}}
		[ -f "$(ARTIFACTS)/src/$$name" ] || curl -fvL -o "$(ARTIFACTS)/src/$$name" "$$url"
	done
	# встроенные расширения перечислены в product.json скачанного vscode. набор
	# и версии отличаются между версиями code-server, поэтому каждая складывает
	# их в свой каталог. имя файла — <publisher>.<name>-<версия>.vsix, из него
	# Dockerfile получает каталог .build/builtInExtensions/<publisher>.<name>
	mkdir -p "$(ARTIFACTS)/src/builtin-$$current"
	tar -xzOf "$(ARTIFACTS)/src/vscode-$$4.tar.gz" "vscode-$$4/product.json" \
		| python3 -c 'import json,sys; [print(e["name"], e["version"]) for e in (json.load(sys.stdin).get("builtInExtensions") or [])]' \
		| while read -r name version; do
			[ -f "$(ARTIFACTS)/src/builtin-$$current/$$name-$$version.vsix" ] && continue
			curl -fvL --compressed -o "$(ARTIFACTS)/src/builtin-$$current/$$name-$$version.vsix" \
				"$(MARKETPLACE_URL)/$${name%%.*}/vsextensions/$${name#*.}/$$version/vspackage"
		done
	# часть зависимостей ставится не из реестра, а по git-протоколу: в
	# package-lock они записаны как git+ssh://git@github.com/... Зеркалим такие
	# репозитории целиком, Dockerfile переписывает resolved на эти зеркала.
	# набор берется из самих lockfile, а не из списка, чтобы не пропустить новую;
	# смотреть надо все шесть десятков (корень, build, remote и каждое
	# расширение), а не только корневой: emmet и copilot тоже тянут с github
	mkdir -p "$(ARTIFACTS)/src/gitdeps-$$current"
	tmp=$$(mktemp -d)
	tar -xzf "$(ARTIFACTS)/src/vscode-$$4.tar.gz" -C "$$tmp" --strip-components=1 --wildcards '*/package-lock.json'
	find "$$tmp" -name package-lock.json -print0 \
		| xargs -0 python3 -c 'import json,sys,re; print("\n".join(sorted({re.sub(r"^git\+","",v["resolved"]).split("#")[0] for f in sys.argv[1:] for v in (json.load(open(f)).get("packages") or {}).values() if (v.get("resolved") or "").startswith(("git+","git:"))})))' \
		| while read -r url; do
			[ -n "$$url" ] || continue
			path=$${url#*github.com/}; owner=$${path%%/*}; repo=$${path##*/}; repo=$${repo%.git}
			out="$(ARTIFACTS)/src/gitdeps-$$current/$$owner-$$repo.tar.gz"
			[ -f "$$out" ] && continue
			m=$$(mktemp -d)
			git clone --quiet --mirror "https://github.com/$$owner/$$repo.git" "$$m/$$owner/$$repo.git"
			tar -czf "$$out" -C "$$m" "$$owner"
			rm -rf "$$m"
		done
	rm -rf "$$tmp"


# Показывает версии зависимостей, которые требуют исходники указанной версии:
# они зашиты внутри vscode (.nvmrc, .npmrc, package-lock.json). Числа отсюда
# переносятся руками в DEPS_<версия>. Работает и для версии, которой еще нет
# в DEPS — коммит сабмодуля и тарбол vscode добывает сам
.PHONY: deps
deps: deps-$(VERSION)

.ONESHELL:
deps-%:
	@current=$(*)
	set -e
	mkdir -p $(ARTIFACTS)/src
	sha=$$(curl -fsSL "https://api.github.com/repos/coder/code-server/contents/lib/vscode?ref=v$$current" \
		| python3 -c 'import json,sys; print(json.load(sys.stdin)["sha"])')
	src='$(ARTIFACTS)/src/vscode-'"$$sha"'.tar.gz'
	[ -f "$$src" ] || curl -fsL -o "$$src" "$(VSCODE_URL)/$$sha"
	nvmrc=$$(tar -xzOf "$$src" vscode-$$sha/.nvmrc)
	electron=$$(tar -xzOf "$$src" vscode-$$sha/.npmrc | sed -n 's/^target="\(.*\)"$$/\1/p')
	ntarget=$$(tar -xzOf "$$src" vscode-$$sha/remote/.npmrc | sed -n 's/^target="\(.*\)"$$/\1/p')
	rg_pkg=$$(tar -xzOf "$$src" vscode-$$sha/package-lock.json | python3 -c 'import json,sys; print([v["version"] for k,v in json.load(sys.stdin)["packages"].items() if k.endswith("node_modules/@vscode/ripgrep")][0])')
	# тег prebuilt-архива зашит в postinstall.js самого пакета @vscode/ripgrep
	rg_tag=$$(curl -fsL "https://registry.npmjs.org/@vscode/ripgrep/-/ripgrep-$$rg_pkg.tgz" | tar -xzO package/lib/postinstall.js 2>/dev/null | sed -n "s/^const VERSION = '\(.*\)';.*/\1/p")
	echo "code-server $$current:"
	echo "  vscode_sha:       $$sha"
	echo "  node (.nvmrc):    $$nvmrc  -> нужен dmp/nodejs не ниже, тот же мажор"
	echo "  electron target:  $$electron"
	echo "  node target:      $$ntarget"
	echo "  ripgrep:          @vscode/ripgrep@$$rg_pkg, prebuilt-тег $${rg_tag:-«нет — новый ripgrep идет npm-пакетами»}"
	echo ""
	echo "строка для DEPS (образы подставьте свои):"
	printf 'DEPS_%s\t:= $$(NODE_IMAGE)\t$$(GCC_IMAGE)\t$$(PYTHON_IMAGE)\t%s\t%s\t\tx86_64-unknown-linux-musl\t\t%s\t\t%s\n' \
		"$$current" "$$sha" "$${rg_tag:-?}" "$$electron" "$$ntarget"


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
	docker build -f Dockerfile $(NO_CACHE_TEXT) --target dist		\
		--build-arg RUNTIME_IMAGE=$$1								\
		--build-arg GCC_IMAGE=$$2									\
		--build-arg PYTHON_IMAGE=$$3								\
		--build-arg CS_VERSION=$$current							\
		--build-arg VSCODE_SHA=$$4									\
		--build-arg RIPGREP_ARCHIVE=ripgrep-$$5-$$6.tar.gz			\
		--build-arg ELECTRON_TARGET=$$7								\
		--build-arg NODE_TARGET=$$8									\
		--tag $(IMAGE):$$current-dist								\
		--progress plain $(BUILD_CONTEXT)


# извлечь скомпилированный дистрибутив (/opt/code-server) из -dist образа
# в artifacts/build/<версия>
.PHONY: extract
extract: extract-$(VERSION)

.ONESHELL:
extract-%: dist-%
	@current=$(*)
	set -e
	img='$(IMAGE):'"$$current"'-dist'
	dst='$(ARTIFACTS)/build/'"$$current"
	# создаём временный контейнер из -dist образа (без запуска) и копируем из него дерево
	cid=$$(docker create $$img)
	trap 'docker rm -f $$cid >/dev/null 2>&1 || true' EXIT
	rm -rf "$$dst"
	mkdir -p "$$dst"
	docker cp "$$cid:$(CS_PATH)/." "$$dst/"
	echo "extracted $$img:$(CS_PATH) -> $$dst"
	du -h --max-depth=1 "$$dst" || true


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
	docker build -f Dockerfile $(NO_CACHE_TEXT)						\
		--build-arg RUNTIME_IMAGE=$$1								\
		--build-arg GCC_IMAGE=$$2									\
		--build-arg PYTHON_IMAGE=$$3								\
		--build-arg CS_VERSION=$$current							\
		--build-arg VSCODE_SHA=$$4									\
		--build-arg RIPGREP_ARCHIVE=ripgrep-$$5-$$6.tar.gz			\
		--build-arg ELECTRON_TARGET=$$7								\
		--build-arg NODE_TARGET=$$8									\
		--tag $(IMAGE):$$current									\
		--tag $(IMAGE):$$major										\
		--progress plain $(BUILD_CONTEXT)


# тестирование собранного образа
.PHONY: test
test: test-$(VERSION)

.ONESHELL:
test-%: build-%
	@current=$(*)
	docker run --rm --entrypoint /bin/bash -v $(TEST_DIR):/test:ro $(IMAGE):$$current -euc '\
		echo "== versions =="; 					\
		node --version; 						\
		code-server --version; 					\
		git --version; 							\
		ssh -V; 								\
		echo "== which =="; 					\
		command -v node; command -v code-server; \
		echo "== whoami (должен быть непривилегированный coder) =="; \
		id; 									\
		echo "== ripgrep (подложен из artifacts, не скачан с github) =="; \
		/usr/local/lib/code-server/lib/vscode/node_modules/@vscode/ripgrep/bin/rg --version | head -1; \
		echo "== нативные модули грузятся =="; 	\
		node /test/native.js; 					\
		echo "== server smoke (start + HTTP + stop) =="; \
		mkdir -p ~/.config/code-server; 		\
		cp /test/config.yaml ~/.config/code-server/config.yaml; \
		code-server --config ~/.config/code-server/config.yaml /tmp & \
		cs_pid=$$!; 							\
		node /test/smoke.js; 					\
		kill $$cs_pid'


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
	for spec in $$v														\
		"$(ARTIFACTS)/src/code-server-$$current-src.tar.gz"				\
		"$(ARTIFACTS)/src/vscode-$$4.tar.gz"							\
		"$(ARTIFACTS)/src/ripgrep-$$5-$$6.tar.gz"						\
		"$(ARTIFACTS)/src/electron-v$$7-headers.tar.gz"					\
	; do rm -f "$$spec" ;
	done
	rm -rf "$(ARTIFACTS)/src/builtin-$$current" "$(ARTIFACTS)/src/gitdeps-$$current"


.PHONY: all
all: sources dist build test
