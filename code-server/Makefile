BASE_DIR      	?= $(abspath .)
ARTIFACTS     	?= $(abspath $(BASE_DIR)/../../ci-artifacts/code-server)
BUILD_CONTEXT 	?= $(BASE_DIR)
LINKED_ARTIFACTS ?= $(BUILD_CONTEXT)/artifacts
TEST_DIR		?= $(BASE_DIR)/test
CS_PATH			?= /opt/code-server


.DEFAULT_GOAL := help

VERSION_CHAIN		?= 4.116.0 4.117.0
VERSION      		?= $(lastword $(VERSION_CHAIN))
# всё с github (тарболы, releases, git-зависимости) — через одну базу
BASE_URL			?= https://github.com
ELECTRON_URL		?= https://www.electronjs.org/headers
MARKETPLACE_URL		?= https://marketplace.visualstudio.com/_apis/public/gallery/publishers
NPM_URL				?= https://registry.npmjs.org
NO_CACHE			?= 0

NODE_IMAGE			?= dmp/nodejs:22.22.1-dist
RUNTIME_IMAGE		?= dmp/glibc:2.28
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

.PHONY: sources-all deps-all dist-all extract-all build-all release-all test-all clean-all
sources-all:		$(addprefix sources-,$(VERSION_CHAIN))
deps-all:		$(addprefix deps-,$(VERSION_CHAIN))
dist-all:		$(addprefix dist-,$(VERSION_CHAIN))
extract-all:		$(addprefix extract-,$(VERSION_CHAIN))
build-all:		$(addprefix build-,$(VERSION_CHAIN))
release-all:		$(addprefix release-,$(VERSION_CHAIN))
test-all:		$(addprefix test-,$(VERSION_CHAIN))
clean-all:		$(addprefix clean-,$(VERSION_CHAIN))


.PHONY: help
help:
	@echo "make:"
	@echo "  make all        собрать всё"
	@echo "  make sources    скачать исходники для $(VERSION)"
	@echo "  make deps       показать версии зависимостей, которые требуют исходники $(VERSION) (для переноса в DEPS)"
	@echo "  make dist       собрать -dist образ $(VERSION) (звенья цепочки — если нужны)"
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
	@echo "  make <стадия>-all  то же для всех версий из VERSION_CHAIN"
	@echo "  FORCE=1            пересобрать, даже если образ уже есть"


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
	$(info ELECTRON_URL   = $(ELECTRON_URL))
	$(info MARKETPLACE_URL = $(MARKETPLACE_URL))
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
	set -e
	set -- $(DEPS_$*)
	mkdir -p $(ARTIFACTS)/src
	for spec in $$v														\
		"$(BASE_URL)/coder/code-server/archive/refs/tags/v$$current.tar.gz|code-server-$$current-src.tar.gz"	\
		"$(BASE_URL)/microsoft/vscode/archive/$$4.tar.gz|vscode-$$4.tar.gz"								\
		"$(BASE_URL)/microsoft/ripgrep-prebuilt/releases/download/$$5/ripgrep-$$5-$$6.tar.gz|"			\
		"$(ELECTRON_URL)/v$$7/node-v$$7-headers.tar.gz|electron-v$$7-headers.tar.gz"					\
	; do url=$${spec%%|*}; name=$${spec#*|}; name=$${name:-$${url##*/}}
		[ -f "$(ARTIFACTS)/src/$$name" ] || curl -fvL -o "$(ARTIFACTS)/src/$$name" "$$url"
	done
	mkdir -p "$(ARTIFACTS)/src/agent-browser-$$current"
	ab_version="$$(tar -xzOf "$(ARTIFACTS)/src/vscode-$$4.tar.gz" "vscode-$$4/package-lock.json" \
		| python3 -c 'import json,sys; print(json.load(sys.stdin)["packages"]["node_modules/agent-browser"]["version"])')"
	[ -f "$(ARTIFACTS)/src/agent-browser-$$current/agent-browser-linux-x64" ] \
		|| curl -fvL -o "$(ARTIFACTS)/src/agent-browser-$$current/agent-browser-linux-x64" \
			"$(BASE_URL)/vercel-labs/agent-browser/releases/download/v$$ab_version/agent-browser-linux-x64"
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
			git clone --quiet --mirror "$(BASE_URL)/$$owner/$$repo.git" "$$m/$$owner/$$repo.git"
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
	# sha сабмодуля lib/vscode: клон без блобов и checkout, только дерево тега
	repo=$$(mktemp -d)
	git clone --quiet --depth 1 --filter=blob:none --no-checkout -b "v$$current" "$(BASE_URL)/coder/code-server.git" "$$repo"
	sha=$$(git -C "$$repo" ls-tree HEAD lib/vscode | awk '{print $$3}')
	rm -rf "$$repo"
	src='$(ARTIFACTS)/src/vscode-'"$$sha"'.tar.gz'
	[ -f "$$src" ] || curl -fsL -o "$$src" "$(BASE_URL)/microsoft/vscode/archive/$$sha.tar.gz"
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
dist-%: $$(call need_dist,$$*)
	@current=$(*)
	set -e
	set -- $(DEPS_$*)
	[ -z "$(call built,$(IMAGE):$*-dist)" ] || { echo "  = $(IMAGE):$*-dist уже собран (FORCE=1 — пересобрать)"; exit 0; }
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
build-%: $$(call need_build,$$*)
	@current=$(*)
	set -e
	set -- $(DEPS_$*)
	major=$${current%%.*}
	[ -z "$(call built,$(IMAGE):$*)" ] || { echo "  = $(IMAGE):$* уже собран (FORCE=1 — пересобрать)"; exit 0; }
	docker build -f Dockerfile $(NO_CACHE_TEXT)						\
		--build-arg DIST_IMAGE=$(IMAGE):$*-dist	\
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
