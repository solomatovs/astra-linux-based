BASE_DIR      	?= $(abspath .)
ARTIFACTS     	?= $(abspath $(BASE_DIR)/../../ci-artifacts/code-server)
BUILD_CONTEXT 	?= $(BASE_DIR)
LINKED_ARTIFACTS ?= $(BUILD_CONTEXT)/artifacts
TEST_DIR		?= $(BASE_DIR)/test
CS_PATH			?= /opt/code-server


.DEFAULT_GOAL := help

VERSION_CHAIN		?= 4.116.0 4.117.0
VERSION      		?= $(lastword $(VERSION_CHAIN))
BASE_URL			?= https://codeload.github.com/coder/code-server/tar.gz/refs/tags
VSCODE_URL			?= https://codeload.github.com/microsoft/vscode/tar.gz
RIPGREP_URL			?= https://github.com/microsoft/ripgrep-prebuilt/releases/download
ELECTRON_URL		?= https://www.electronjs.org/headers
MARKETPLACE_URL		?= https://marketplace.visualstudio.com/_apis/public/gallery/publishers
GITDEPS_URL			?= https://github.com
GITHUB_API_URL		?= https://api.github.com
NPM_URL				?= https://registry.npmjs.org
AGENT_BROWSER_URL	?= https://github.com/vercel-labs/agent-browser/releases/download
NO_CACHE			?= 0

NODE_IMAGE			?= dmp/nodejs:22.22.1-dist
RUNTIME_IMAGE			?= dmp/glibc:2.28
GCC_IMAGE			?= dmp/gcc:12.4.0-dist
PYTHON_IMAGE		?= dmp/python:3.10-dist

BUILDX_IMAGE		?= dmp/docker-buildx:0.30.0-dist
COMPOSE_IMAGE		?= dmp/docker-compose:2.38.2-dist

# портативный релиз: одна папка со всем (бандл, node, библиотеки, env, unit, data)
RELEASE_NAME		:= code-server-$(VERSION)
STAGE_DIR			?= $(BASE_DIR)/release/$(RELEASE_NAME)
# куда релиз разворачивается на сервере (совпадает с CS_DIR внутри образа)
DEST_DIR			?= /app/code-server/$(VERSION)

DEPS_4.116.0	:= $(NODE_IMAGE)	$(GCC_IMAGE)	$(PYTHON_IMAGE)	560a9dba96f961efea7b1612916f89e5d5d4d679	v15.0.1		x86_64-unknown-linux-musl		39.8.7		22.22.1
DEPS_4.117.0	:= $(NODE_IMAGE)	$(GCC_IMAGE)	$(PYTHON_IMAGE)	10c8e557c8b9f9ed0a87f61f1c9a44bde731c409	v15.0.1		x86_64-unknown-linux-musl		39.8.7		22.22.1

IMAGE			:= dmp/code-server
FIRST_VERSION	:= $(firstword $(VERSION_CHAIN))
NO_CACHE_TEXT	:= $(if $(filter 1,$(NO_CACHE)),--no-cache,)


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
	@echo "  make release    собрать портативную папку релиза ($(STAGE_DIR))"
	@echo "  make export     скопировать релиз на сервер ($(DEST_DIR)) + установить systemd-юнит"
	@echo "  make test       проверить образ $(IMAGE): версии, запуск сервера, HTTP-ответ"
	@echo ""
	@echo "  Цепочка: $(VERSION_CHAIN)"
	@echo "  Собирается из исходников по upstream ci/: code-server + сабмодуль lib/vscode,"
	@echo "  патчи из patches/ через quilt, затем gulp vscode-reh-web-linux-x64-min"
	@echo "  и build-release.sh."
	@echo "  Всё, что апстрим качает мимо npm-реестра (ripgrep, заголовки node и electron,"
	@echo "  node для reh-бандла, встроенные расширения, бинарник agent-browser), заранее"
	@echo "  лежит в $(ARTIFACTS)/src и подкладывается до запуска сборки — см. sources и Dockerfile."
	@echo "  Зависимости задаются пословно в DEPS_<версия>."
	@echo "  Другая версия: make all VERSION=4.116.0"
	@echo ""


.PHONY: debug
debug:
	$(info BASE_DIR       = $(BASE_DIR))
	$(info ARTIFACTS      = $(ARTIFACTS))
	$(info CS_PATH        = $(CS_PATH))
	$(info RELEASE_NAME   = $(RELEASE_NAME))
	$(info STAGE_DIR      = $(STAGE_DIR))
	$(info DEST_DIR       = $(DEST_DIR))
	$(info BUILD_CONTEXT  = $(BUILD_CONTEXT))
	$(info LINKED_ARTIFACTS = $(LINKED_ARTIFACTS))
	$(info TEST_DIR       = $(TEST_DIR))
	$(info VERSION_CHAIN  = $(VERSION_CHAIN))
	$(info VERSION        = $(VERSION))
	$(info FIRST_VERSION  = $(FIRST_VERSION))
	$(info BASE_URL       = $(BASE_URL))
	$(info VSCODE_URL     = $(VSCODE_URL))
	$(info RIPGREP_URL    = $(RIPGREP_URL))
	$(info ELECTRON_URL   = $(ELECTRON_URL))
	$(info MARKETPLACE_URL = $(MARKETPLACE_URL))
	$(info GITDEPS_URL    = $(GITDEPS_URL))
	$(info GITHUB_API_URL = $(GITHUB_API_URL))
	$(info NPM_URL        = $(NPM_URL))
	$(info NODE_IMAGE     = $(NODE_IMAGE))
	$(info RUNTIME_IMAGE  = $(RUNTIME_IMAGE))
	$(info BUILDX_IMAGE   = $(BUILDX_IMAGE))
	$(info COMPOSE_IMAGE  = $(COMPOSE_IMAGE))
	$(info NO_CACHE       = $(NO_CACHE))
	$(info NO_CACHE_TEXT  = $(NO_CACHE_TEXT))
	$(info IMAGE          = $(IMAGE):$(VERSION))
	$(info DEPS($(VERSION)) = $(DEPS_$(VERSION)))
	@true


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


.PHONY: sources
sources: sources-$(VERSION)

.ONESHELL:
sources-%:
	@current=$(*)
	@prev='$(call prev_of,$*)'
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
	mkdir -p "$(ARTIFACTS)/src/agent-browser-$$current"
	ab_version="$$(tar -xzOf "$(ARTIFACTS)/src/vscode-$$4.tar.gz" "vscode-$$4/package-lock.json" \
		| python3 -c 'import json,sys; print(json.load(sys.stdin)["packages"]["node_modules/agent-browser"]["version"])')"
	[ -f "$(ARTIFACTS)/src/agent-browser-$$current/agent-browser-linux-x64" ] \
		|| curl -fvL -o "$(ARTIFACTS)/src/agent-browser-$$current/agent-browser-linux-x64" \
			"$(AGENT_BROWSER_URL)/v$$ab_version/agent-browser-linux-x64"
	mkdir -p "$(ARTIFACTS)/src/builtin-$$current"
	tar -xzOf "$(ARTIFACTS)/src/vscode-$$4.tar.gz" "vscode-$$4/product.json" \
		| python3 -c 'import json,sys; [print(e["name"], e["version"]) for e in (json.load(sys.stdin).get("builtInExtensions") or [])]' \
		| while read -r name version; do
			[ -f "$(ARTIFACTS)/src/builtin-$$current/$$name-$$version.vsix" ] && continue
			curl -fvL --compressed -o "$(ARTIFACTS)/src/builtin-$$current/$$name-$$version.vsix" \
				"$(MARKETPLACE_URL)/$${name%%.*}/vsextensions/$${name#*.}/$$version/vspackage"
		done
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
			git clone --quiet --mirror "$(GITDEPS_URL)/$$owner/$$repo.git" "$$m/$$owner/$$repo.git"
			tar -czf "$$out" -C "$$m" "$$owner"
			rm -rf "$$m"
		done
	rm -rf "$$tmp"


.PHONY: deps
deps: deps-$(VERSION)

.ONESHELL:
deps-%:
	@current=$(*)
	set -e
	mkdir -p $(ARTIFACTS)/src
	sha=$$(curl -fsSL "$(GITHUB_API_URL)/repos/coder/code-server/contents/lib/vscode?ref=v$$current" \
		| python3 -c 'import json,sys; print(json.load(sys.stdin)["sha"])')
	src='$(ARTIFACTS)/src/vscode-'"$$sha"'.tar.gz'
	[ -f "$$src" ] || curl -fsL -o "$$src" "$(VSCODE_URL)/$$sha"
	nvmrc=$$(tar -xzOf "$$src" vscode-$$sha/.nvmrc)
	electron=$$(tar -xzOf "$$src" vscode-$$sha/.npmrc | sed -n 's/^target="\(.*\)"$$/\1/p')
	ntarget=$$(tar -xzOf "$$src" vscode-$$sha/remote/.npmrc | sed -n 's/^target="\(.*\)"$$/\1/p')
	rg_pkg=$$(tar -xzOf "$$src" vscode-$$sha/package-lock.json | python3 -c 'import json,sys; print([v["version"] for k,v in json.load(sys.stdin)["packages"].items() if k.endswith("node_modules/@vscode/ripgrep")][0])')
	rg_tag=$$(curl -fsL "$(NPM_URL)/@vscode/ripgrep/-/ripgrep-$$rg_pkg.tgz" | tar -xzO package/lib/postinstall.js 2>/dev/null | sed -n "s/^const VERSION = '\(.*\)';.*/\1/p")
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


.PHONY: dist
dist: dist-$(VERSION)

.ONESHELL:
dist-%: sources-%
	@current=$(*)
	prev='$(call prev_of,$*)'
	[ -n "$$prev" ] && $(MAKE) dist-$$prev || true
	set -e
	set -- $(DEPS_$*)
	$(MAKE) --no-print-directory artifacts-link
	docker build -f Dockerfile $(NO_CACHE_TEXT) --target dist		\
		--build-arg NODE_IMAGE=$$1									\
		--build-arg RUNTIME_IMAGE=$(RUNTIME_IMAGE)						\
		--build-arg GCC_IMAGE=$$2									\
		--build-arg PYTHON_IMAGE=$$3								\
		--build-arg CS_VERSION=$$current							\
		--build-arg VSCODE_SHA=$$4									\
		--build-arg RIPGREP_ARCHIVE=ripgrep-$$5-$$6.tar.gz			\
		--build-arg ELECTRON_TARGET=$$7								\
		--build-arg NODE_TARGET=$$8									\
		--tag $(IMAGE):$$current-dist								\
		--progress plain $(BUILD_CONTEXT)


.PHONY: extract
extract: extract-$(VERSION)

.ONESHELL:
extract-%: dist-%
	@current=$(*)
	set -e
	img='$(IMAGE):'"$$current"'-dist'
	dst='$(ARTIFACTS)/build/'"$$current"
	cid=$$(docker create $$img)
	trap 'docker rm -f $$cid >/dev/null 2>&1 || true' EXIT
	rm -rf "$$dst"
	mkdir -p "$$dst"
	docker cp "$$cid:$(CS_PATH)/." "$$dst/"
	echo "extracted $$img:$(CS_PATH) -> $$dst"
	du -h --max-depth=1 "$$dst" || true


.PHONY: build
build: build-$(VERSION)

.ONESHELL:
build-%: dist-%
	@current=$(*)
	prev='$(call prev_of,$*)'
	[ -n "$$prev" ] && $(MAKE) build-$$prev || true
	set -e
	set -- $(DEPS_$*)
	major=$${current%%.*}
	$(MAKE) --no-print-directory artifacts-link
	docker build -f Dockerfile $(NO_CACHE_TEXT)						\
		--build-arg NODE_IMAGE=$$1									\
		--build-arg RUNTIME_IMAGE=$(RUNTIME_IMAGE)						\
		--build-arg GCC_IMAGE=$$2									\
		--build-arg PYTHON_IMAGE=$$3								\
		--build-arg CS_VERSION=$$current							\
		--build-arg VSCODE_SHA=$$4									\
		--build-arg RIPGREP_ARCHIVE=ripgrep-$$5-$$6.tar.gz			\
		--build-arg ELECTRON_TARGET=$$7								\
		--build-arg NODE_TARGET=$$8									\
		--build-arg BUILDX_IMAGE=$(BUILDX_IMAGE)					\
		--build-arg COMPOSE_IMAGE=$(COMPOSE_IMAGE)					\
		--tag $(IMAGE):$$current									\
		--tag $(IMAGE):$$major										\
		--progress plain $(BUILD_CONTEXT)


# портативный релиз: собрать runtime-образ и достать ${CS_DIR} docker cp
.PHONY: release
release: release-$(VERSION)

.ONESHELL:
release-%: build-%
	@current=$(*)
	img='$(IMAGE):'"$$current"
	cs_dir='/app/code-server/'"$$current"
	rm -rf "$(STAGE_DIR)"
	mkdir -p "$(STAGE_DIR)"
	cid=$$(docker create $$img)
	trap 'docker rm -f $$cid >/dev/null 2>&1 || true' EXIT
	docker cp "$$cid:$$cs_dir/." "$(STAGE_DIR)/"
	docker rm "$$cid" >/dev/null
	trap - EXIT
	echo "release: code-server-$$current"       > "$(STAGE_DIR)/VERSION.txt"
	echo "version: $$current"                  >> "$(STAGE_DIR)/VERSION.txt"
	echo "source:  $(BASE_DIR)"                >> "$(STAGE_DIR)/VERSION.txt"
	chown -R "$$(stat -c '%u:%g' "$(STAGE_DIR)")" "$(STAGE_DIR)"
	echo ">>> релиз готов: $(STAGE_DIR)  ($$(du -sh --apparent-size "$(STAGE_DIR)" | cut -f1))"
	echo "    на сервере: $(DEST_DIR) — путь зашит в conf/code-server.env и conf/code-server.service"


# скопировать собранный релиз в $(DEST_DIR) (через sudo, если каталог не пишется)
.PHONY: export
export:
	@[ -d "$(STAGE_DIR)/code-server" ] || { echo "нет собранного релиза — сначала: make release" >&2; exit 1; }
	echo ">>> экспорт: $(STAGE_DIR) -> $(DEST_DIR)"
	SUDO=""; [ -w "$$(dirname $(DEST_DIR))" ] || SUDO=sudo
	$$SUDO rm -rf "$(DEST_DIR)"
	$$SUDO mkdir -p "$$(dirname $(DEST_DIR))"
	$$SUDO cp -a "$(STAGE_DIR)/." "$(DEST_DIR)"
	$$SUDO chown -R "$$(stat -c '%u:%g' "$(STAGE_DIR)")" "$(DEST_DIR)"
	echo ">>> готово: $(DEST_DIR)"
	echo "    запуск: sudo cp $(DEST_DIR)/conf/code-server.service /etc/systemd/system/ && sudo systemctl daemon-reload && sudo systemctl enable --now code-server"


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
		docker-buildx version; 					\
		docker-compose version; 				\
		echo "== which =="; 					\
		command -v node; command -v code-server; \
		echo "== docker cli-плагины =="; 		\
		ls -l /usr/local/lib/docker/cli-plugins; \
		echo "== плагины работают (без демона: discovery + разбор конфигов) =="; \
		/usr/local/lib/docker/cli-plugins/docker-buildx docker-cli-plugin-metadata  | grep -q "\"Vendor\""; \
		echo "  ok    docker buildx  — metadata для docker cli"; \
		/usr/local/lib/docker/cli-plugins/docker-compose docker-cli-plugin-metadata | grep -q "\"Vendor\""; \
		echo "  ok    docker compose — metadata для docker cli"; \
		docker-compose -f /test/compose.yaml config | grep -q "dmp/code-server-test:1"; \
		echo "  ok    docker-compose config"; 	\
		(cd /test && docker-buildx bake -f compose.yaml --print 2>/dev/null) | grep -q "dmp/code-server-test:1"; \
		echo "  ok    docker-buildx bake --print"; \
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
	rm -rf "$(LINKED_ARTIFACTS)"

.ONESHELL:
clean-%:
	@current=$(*)
	@prev='$(call prev_of,$*)'
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
	rm -rf "$(ARTIFACTS)/src/builtin-$$current" "$(ARTIFACTS)/src/gitdeps-$$current" "$(ARTIFACTS)/src/agent-browser-$$current"


.PHONY: all
all: sources dist build test
