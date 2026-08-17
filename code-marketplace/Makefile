BASE_DIR      	?= $(abspath .)
ARTIFACTS     	?= $(abspath $(BASE_DIR)/../../ci-artifacts/code-marketplace)
BUILD_CONTEXT 	?= $(BASE_DIR)
# рабочая копия $(ARTIFACTS) внутри контекста сборки (хардлинки, см. artifacts-link)
LINKED_ARTIFACTS ?= $(BUILD_CONTEXT)/artifacts
TEST_DIR		?= $(BASE_DIR)/test

# путь, по которому дистрибутив лежит внутри -dist образа (CM_PATH в Dockerfile)
CM_PATH			?= /opt/code-marketplace


.DEFAULT_GOAL := help

# список версий, которые поддерживает сборщик
VERSION_CHAIN		?= 2.4.0 2.4.2
# целевая версия сборки. по умолчанию берется последняя версия
VERSION      		?= $(lastword $(VERSION_CHAIN))
# базовый url адрес для скачивания исходников собираемых в Dockerfile
BASE_URL			?= https://codeload.github.com/coder/code-marketplace/tar.gz/refs/tags
# прокси go-модулей, используется только на этапе sources
GOPROXY				?= https://proxy.golang.org,direct
GOSUMDB				?= sum.golang.org
# выполнять ли сброс кэша: docker build --no-cache
NO_CACHE			?= 0

GLIBC_IMAGE			?= dmp/glibc:2.28

# зависимости для сборки разных версии
# go_image: версия не ниже директивы go в go.mod тега, иначе go полезет качать
# тулчейн. узнать значение для новой версии: make deps VERSION=<версия>
# версия           runtime_image	go_image
DEPS_2.4.0		:= $(GLIBC_IMAGE)	dmp/golang:1.24.5-dist
DEPS_2.4.2		:= $(GLIBC_IMAGE)	dmp/golang:1.25.12-dist

# базовое название для образа
IMAGE			:= dmp/code-marketplace
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

.PHONY: sources-all deps-all dist-all extract-all build-all test-all clean-all
sources-all:		$(addprefix sources-,$(VERSION_CHAIN))
deps-all:		$(addprefix deps-,$(VERSION_CHAIN))
dist-all:		$(addprefix dist-,$(VERSION_CHAIN))
extract-all:		$(addprefix extract-,$(VERSION_CHAIN))
build-all:		$(addprefix build-,$(VERSION_CHAIN))
test-all:		$(addprefix test-,$(VERSION_CHAIN))
clean-all:		$(addprefix clean-,$(VERSION_CHAIN))


.PHONY: help
help:
	@echo "make:"
	@echo "  make all        собрать всё"
	@echo "  make sources    скачать исходники и вендорить go-модули для $(VERSION)"
	@echo "  make deps       показать версию go, которую требуют исходники $(VERSION) (для переноса в DEPS)"
	@echo "  make dist       собрать -dist образ $(VERSION) (звенья цепочки — если нужны)"
	@echo "  make extract    достать дистрибутив ($(CM_PATH)) из -dist образа в $(ARTIFACTS)/build/<версия>"
	@echo "  make build      собрать образ $(IMAGE)"
	@echo "  make test       проверить образ $(IMAGE): version, add .vsix, extensionquery, assets, healthz"
	@echo ""
	@echo "  Цепочка: $(VERSION_CHAIN)"
	@echo "  Реестр расширений для code-server/VSCodium: статический go-бинарник,"
	@echo "  раздает .vsix из каталога по gallery API."
	@echo "  vendor/ в апстриме нет, поэтому make sources вендорит go-модули в"
	@echo "  $(ARTIFACTS)/src, а сборка идет с GOFLAGS=-mod=vendor GOPROXY=off."
	@echo "  Зависимости задаются пословно в DEPS_<версия>."
	@echo "  Другая версия: make all VERSION=2.4.0"
	@echo ""
	@echo "  Подключение к code-server (только через https):"
	@echo "    EXTENSIONS_GALLERY='{\"serviceUrl\":\"https://<домен>/api\","
	@echo "      \"itemUrl\":\"https://<домен>/item\","
	@echo "      \"resourceUrlTemplate\":\"https://<домен>/files/{publisher}/{name}/{version}/{path}\"}'"
	@echo ""
	@echo "  make <стадия>-all  то же для всех версий из VERSION_CHAIN"
	@echo "  FORCE=1            пересобрать, даже если образ уже есть"


.PHONY: debug
debug:
	$(info BASE_DIR       = $(BASE_DIR))
	$(info ARTIFACTS      = $(ARTIFACTS))
	$(info CM_PATH        = $(CM_PATH))
	$(info BUILD_CONTEXT  = $(BUILD_CONTEXT))
	$(info LINKED_ARTIFACTS = $(LINKED_ARTIFACTS))
	$(info TEST_DIR       = $(TEST_DIR))
	$(info VERSION_CHAIN  = $(VERSION_CHAIN))
	$(info VERSION        = $(VERSION))
	$(info FIRST_VERSION  = $(FIRST_VERSION))
	$(info BASE_URL       = $(BASE_URL))
	$(info GOPROXY        = $(GOPROXY))
	$(info GOSUMDB        = $(GOSUMDB))
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
	src="$(ARTIFACTS)/src/code-marketplace-$$current-src.tar.gz"
	[ -f "$$src" ] || curl -fvL -o "$$src" "$(BASE_URL)/v$$current"
	# go-модули — единственное, что сборка тянула бы из сети. вендорим заранее тем
	# же тулчейном, которым потом собираем: версия go влияет на состав vendor/
	vendor="$(ARTIFACTS)/src/vendor-$$current.tar.gz"
	if [ ! -f "$$vendor" ]; then
		tmp=$$(mktemp -d)
		trap 'rm -rf "$$tmp"' EXIT
		tar -xzf "$$src" -C "$$tmp" --strip-components=1
		# в -dist образе тулчейн лежит деревом /opt/go/usr/local/go, запускаем
		# go оттуда напрямую. --user: иначе vendor/ создается от root
		docker run --rm													\
			--user "$$(id -u):$$(id -g)"								\
			-v "$$tmp:/src" -w /src										\
			-e GOROOT=/opt/go/usr/local/go								\
			-e GOFLAGS=-mod=mod											\
			-e GOTOOLCHAIN=local										\
			-e GOPROXY="$(GOPROXY)"										\
			-e GOSUMDB="$(GOSUMDB)"										\
			-e HOME=/tmp												\
			-e GOCACHE=/tmp/gocache										\
			-e GOMODCACHE=/tmp/gomodcache								\
			--entrypoint /opt/go/usr/local/go/bin/go					\
			$$2 mod vendor
		tar -czf "$$vendor" -C "$$tmp" vendor
		du -h "$$vendor"
	fi


# Показывает версию go, которую требуют исходники указанной версии: она зашита
# в go.mod тега. Число отсюда переносится руками в DEPS_<версия> — образ
# dmp/golang должен быть не ниже. Работает и для версии, которой еще нет в DEPS
.PHONY: deps
deps: deps-$(VERSION)

.ONESHELL:
deps-%:
	@current=$(*)
	set -e
	mkdir -p $(ARTIFACTS)/src
	src="$(ARTIFACTS)/src/code-marketplace-$$current-src.tar.gz"
	[ -f "$$src" ] || curl -fsL -o "$$src" "$(BASE_URL)/v$$current"
	gomod=$$(tar -xzOf "$$src" "code-marketplace-$$current/go.mod")
	go_directive=$$(echo "$$gomod" | sed -n 's/^go \(.*\)$$/\1/p')
	toolchain=$$(echo "$$gomod" | sed -n 's/^toolchain go\(.*\)$$/\1/p')
	echo "code-marketplace $$current:"
	echo "  go (go.mod):      $$go_directive  -> нужен dmp/golang не ниже этой версии"
	echo "  toolchain:        $${toolchain:-«не задан — достаточно go >= директивы выше»}"
	echo ""
	echo "доступные образы dmp/golang:"
	docker images --format '  {{.Repository}}:{{.Tag}}' 'dmp/golang' | grep -- '-dist' | sort -V
	echo ""
	echo "строка для DEPS (образ подставьте свой):"
	printf 'DEPS_%s\t\t:= $$(GLIBC_IMAGE)\tdmp/golang:%s-dist\n' "$$current" "?"


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
		--build-arg RUNTIME_IMAGE=$$1							\
		--build-arg GO_IMAGE=$$2								\
		--build-arg CM_VERSION=$$current						\
		--tag $(IMAGE):$$current-dist							\
		--progress plain $(BUILD_CONTEXT)


# извлечь скомпилированный дистрибутив (/opt/code-marketplace) из -dist образа
# в artifacts/build/<версия>
.PHONY: extract
extract: extract-$(VERSION)

.ONESHELL:
extract-%: dist-%
	@current=$(*)
	set -e
	img='$(IMAGE):'"$$current"'-dist'
	dst='$(ARTIFACTS)/build/'"$$current"
	# создаём временный контейнер из -dist образа (без запуска) и копируем из него дерево.
	# --entrypoint обязателен: у dist-стадии нет ни ENTRYPOINT, ни CMD
	cid=$$(docker create --entrypoint /bin/true $$img)
	trap 'docker rm -f $$cid >/dev/null 2>&1 || true' EXIT
	rm -rf "$$dst"
	mkdir -p "$$dst"
	docker cp "$$cid:$(CM_PATH)/." "$$dst/"
	echo "extracted $$img:$(CM_PATH) -> $$dst"
	du -h --max-depth=1 "$$dst" || true


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
	docker build -f Dockerfile $(NO_CACHE_TEXT)					\
		--build-arg RUNTIME_IMAGE=$$1							\
		--build-arg GO_IMAGE=$$2								\
		--build-arg CM_VERSION=$$current						\
		--tag $(IMAGE):$$current								\
		--tag $(IMAGE):$$major									\
		--progress plain $(BUILD_CONTEXT)


# тестирование собранного образа
.PHONY: test
test: test-$(VERSION)

.ONESHELL:
test-%: build-%
	@current=$(*)
	set -e
	# .vsix собирается на хосте: в рантайм-образе нет ни python3, ни zip
	vsix='$(ARTIFACTS)/test/'"$$current"
	rm -rf "$$vsix"
	mkdir -p "$$vsix"
	python3 $(TEST_DIR)/mkvsix.py --outdir "$$vsix" \
		--publisher dmp --name hello --version 1.0.0
	python3 $(TEST_DIR)/mkvsix.py --outdir "$$vsix" \
		--publisher dmp --name hello --version 1.1.0
	docker run --rm --entrypoint /bin/bash					\
		-v $(TEST_DIR):/test:ro								\
		-v "$$vsix:/vsix:ro"								\
		$(IMAGE):$$current -euc '							\
		echo "== version =="; 								\
		code-marketplace version; 							\
		code-marketplace version | grep -q "'"$$current"'"; \
		echo "  ok    бинарник знает свою версию (ldflags buildinfo.tag)"; \
		echo "== add =="; 									\
		export EXTDIR=/tmp/extensions; 						\
		code-marketplace add /vsix --extensions-dir $$EXTDIR; \
		echo "== раскладка каталога расширений =="; 		\
		find $$EXTDIR -maxdepth 3 -mindepth 3 | sort; 		\
		echo "== server =="; 								\
		code-marketplace server --extensions-dir $$EXTDIR --address 127.0.0.1:3001 & \
		cm_pid=$$!; 										\
		for i in $$(seq 50); do 							\
			curl -fsS http://127.0.0.1:3001/healthz >/dev/null 2>&1 && break; \
			sleep 0.2; 										\
		done; 												\
		curl -fsS http://127.0.0.1:3001/healthz; 			\
		echo "  ok    /healthz"; 							\
		echo "== extensionquery (то, что дергает code-server при поиске) =="; \
		resp=$$(curl -fsS http://127.0.0.1:3001/api/extensionquery \
			-H "Accept: application/json;api-version=3.0-preview.1" \
			-H "Content-Type: application/json" 			\
			--data-binary @/test/extensionquery.json); 		\
		echo "$$resp" | head -c 400; echo; 					\
		echo "$$resp" | grep -q "\"extensionName\":\"hello\""; \
		echo "  ok    расширение найдено в выдаче"; 		\
		echo "$$resp" | grep -q "\"version\":\"1.1.0\""; 	\
		echo "  ok    последняя версия 1.1.0 отдана"; 		\
		echo "== assets (то, откуда code-server качает .vsix) =="; \
		curl -fsSL -o /tmp/dl.vsix -w "  package: HTTP %{http_code}, %{size_download} байт\n" \
			http://127.0.0.1:3001/assets/dmp/hello/1.1.0/Microsoft.VisualStudio.Services.VSIXPackage; \
		curl -fsSL -o /tmp/manifest.json -w "  manifest: HTTP %{http_code}, %{size_download} байт\n" \
			http://127.0.0.1:3001/assets/dmp/hello/1.1.0/Microsoft.VisualStudio.Code.Manifest; \
		grep -q "\"name\": \"hello\"" /tmp/manifest.json; 	\
		echo "  ok    assets отдаются"; 					\
		echo "== files (resourceUrlTemplate для web-расширений) =="; \
		curl -fsS -o /dev/null -w "  files: HTTP %{http_code}\n" \
			http://127.0.0.1:3001/files/dmp/hello/1.1.0/extension/package.json; \
		echo "== remove =="; 								\
		code-marketplace remove dmp.hello@1.0.0 --extensions-dir $$EXTDIR; \
		test ! -d $$EXTDIR/dmp/hello/1.0.0; 				\
		echo "  ok    версия удалена"; 						\
		kill $$cm_pid'


.PHONY: clean
clean: clean-$(VERSION)
	rm -rf "$(LINKED_ARTIFACTS)"

.ONESHELL:
clean-%:
	@current=$(*)
	set -e
	rm -f "$(ARTIFACTS)/src/code-marketplace-$$current-src.tar.gz"
	rm -f "$(ARTIFACTS)/src/vendor-$$current.tar.gz"
	rm -rf "$(ARTIFACTS)/test/$$current"


.PHONY: all
all: sources dist build test
