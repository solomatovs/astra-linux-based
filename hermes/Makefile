BASE_DIR      	?= $(abspath .)
ARTIFACTS     	?= $(abspath $(BASE_DIR)/../../ci-artifacts/hermes)
BUILD_CONTEXT 	?= $(BASE_DIR)
# рабочая копия $(ARTIFACTS) внутри контекста сборки (хардлинки, см. artifacts-link)
LINKED_ARTIFACTS ?= $(BUILD_CONTEXT)/artifacts
TEST_DIR		?= $(BASE_DIR)/test

# путь, по которому дистрибутив лежит внутри -dist образа (HERMES_PATH в Dockerfile)
HERMES_PATH		?= /opt/hermes


.DEFAULT_GOAL := help

# список версий, которые поддерживает сборщик
# версии независимы друг от друга, собираются по отдельности
VERSION_CHAIN		?= 2026.7.20
# целевая версия сборки. по умолчанию берется последняя версия
VERSION      		?= $(lastword $(VERSION_CHAIN))
# форк hermes-agent, из тегов которого собирается образ
BASE_URL			?= https://codeload.github.com/solomatovs/hermes-agent/tar.gz/refs/tags
# s6-overlay: супервизор, он же PID 1 в образе (апстрим ставит его тем же способом)
S6_URL				?= https://github.com/just-containers/s6-overlay/releases/download
# ripgrep: в репозиториях astra пакета нет, кладём prebuilt статический бинарник
RIPGREP_URL			?= https://github.com/BurntSushi/ripgrep/releases/download
# libolm: нужен python-olm из matrix-extra, пакета в astra нет — собираем стадией
OLM_URL				?= https://gitlab.matrix.org/matrix-org/olm/-/archive
# выполнять ли сброс кэша: docker build --no-cache
NO_CACHE			?= 0

NODE_IMAGE			?= dmp/nodejs:22.22.1
PYTHON_IMAGE		?= dmp/python:3.13-dist
GCC_IMAGE			?= dmp/gcc:12.4.0-dist
UV_IMAGE			?= dmp/uv:0.9.26-dist
SQLITE_IMAGE		?= dmp/sqlite:3.53.4-dist
CMAKE_IMAGE			?= dmp/cmake:3.27.9-dist

# s6-overlay ставится бинарными тарболами, версия и суммы взяты из
# docker/Dockerfile собираемого тега (см. make deps)
S6_VERSION			?= 3.2.3.0
S6_SHA_NOARCH		?= b720f9d9340efc8bb07528b9743813c836e4b02f8693d90241f047998b4c53cf
S6_SHA_X86_64		?= a93f02882c6ed46b21e7adb5c0add86154f01236c93cd82c7d682722e8840563
S6_SHA_SYMLINKS		?= a60dc5235de3ecbcf874b9c1f18d73263ab99b289b9329aa950e8729c4789f0e

# набор extra из pyproject, который ставится в образ
UV_EXTRAS			?= all messaging anthropic bedrock azure-identity hindsight matrix

# playwright оставляет только сборку chromium под ubuntu24.04 (GLIBC_2.39),
# базовому образу столько не даёт astra. браузерные тулы работают без него:
# agent-browser (musl-бинарь) + системный chromium, см. WITH_CHROMIUM
WITH_PLAYWRIGHT		?= 0
PLAYWRIGHT_PLATFORM	?= ubuntu24.04

# системный chromium из репозиториев astra, на него указывает
# AGENT_BROWSER_EXECUTABLE_PATH. без него браузерным тулам нужен свой браузер
WITH_CHROMIUM		?= 1

# зависимости для сборки разных версии
# ripgrep: версия prebuilt-архива, кладётся в /usr/local/bin
# olm:     версия libolm, собирается стадией из тарбола gitlab.matrix.org
# узнать значения для новой версии: make deps VERSION=<версия>
# версия           runtime_image	python_image		gcc_image		uv_image		sqlite_image		cmake_image		ripgrep		olm
DEPS_2026.7.20	:= $(NODE_IMAGE)	$(PYTHON_IMAGE)		$(GCC_IMAGE)	$(UV_IMAGE)		$(SQLITE_IMAGE)		$(CMAKE_IMAGE)	14.1.1		3.2.16

# базовое название для образа
IMAGE			:= dmp/hermes
FIRST_VERSION	:= $(firstword $(VERSION_CHAIN))
NO_CACHE_TEXT	:= $(if $(filter 1,$(NO_CACHE)),--no-cache,)
RIPGREP_TARGET	:= x86_64-unknown-linux-musl


.PHONY: help
help:
	@echo "make:"
	@echo "  make all        собрать всё"
	@echo "  make sources    скачать исходники и внешние тарболы для версии $(VERSION)"
	@echo "  make deps       показать версии, которые требует docker/Dockerfile тега $(VERSION)"
	@echo "  make dist       собрать $(IMAGE):$(VERSION)-dist"
	@echo "  make extract    достать дистрибутив ($(HERMES_PATH)) из -dist образа в $(ARTIFACTS)/build/<версия>"
	@echo "  make build      собрать образ $(IMAGE)"
	@echo "  make test       проверить образ: hermes/node/python, sqlite, s6, api-server"
	@echo ""
	@echo "  Версии: $(VERSION_CHAIN)"
	@echo "  Собирается тег форка solomatovs/hermes-agent: python-часть через uv,"
	@echo "  фронты web/ и ui-tui/ через npm, супервизор s6-overlay как PID 1."
	@echo "  libsqlite3 берётся из dmp/sqlite (FTS5 trigram + фикс WAL-reset),"
	@echo "  libolm собирается стадией, ripgrep кладётся prebuilt-бинарником:"
	@echo "  ни того, ни другого в репозиториях astra нет."
	@echo "  Браузерные тулы: musl-сборка agent-browser (обычная требует"
	@echo "  GLIBC_2.30) поверх системного chromium, вместо загрузки playwright'ом."
	@echo "  Зависимости задаются пословно в DEPS_<версия>."
	@echo "  Другая версия: make all VERSION=2026.7.20"
	@echo ""


.PHONY: debug
debug:
	$(info BASE_DIR       = $(BASE_DIR))
	$(info ARTIFACTS      = $(ARTIFACTS))
	$(info HERMES_PATH    = $(HERMES_PATH))
	$(info BUILD_CONTEXT  = $(BUILD_CONTEXT))
	$(info LINKED_ARTIFACTS = $(LINKED_ARTIFACTS))
	$(info TEST_DIR       = $(TEST_DIR))
	$(info VERSION_CHAIN  = $(VERSION_CHAIN))
	$(info VERSION        = $(VERSION))
	$(info FIRST_VERSION  = $(FIRST_VERSION))
	$(info BASE_URL       = $(BASE_URL))
	$(info S6_URL         = $(S6_URL))
	$(info S6_VERSION     = $(S6_VERSION))
	$(info RIPGREP_URL    = $(RIPGREP_URL))
	$(info UV_EXTRAS      = $(UV_EXTRAS))
	$(info WITH_PLAYWRIGHT= $(WITH_PLAYWRIGHT))
	$(info PLAYWRIGHT_PLATFORM = $(PLAYWRIGHT_PLATFORM))
	$(info WITH_CHROMIUM   = $(WITH_CHROMIUM))
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


# Скачиваем исходники и внешние тарболы для сборки дистрибутива
.PHONY: sources
sources: sources-$(VERSION)

.ONESHELL:
sources-%:
	@current=$(*)
	set -e
	set -- $(DEPS_$*)
	mkdir -p $(ARTIFACTS)/src
	for spec in $$v																					\
		"$(BASE_URL)/v$$current|hermes-agent-$$current-src.tar.gz"									\
		"$(S6_URL)/v$(S6_VERSION)/s6-overlay-noarch.tar.xz|s6-overlay-$(S6_VERSION)-noarch.tar.xz"	\
		"$(S6_URL)/v$(S6_VERSION)/s6-overlay-x86_64.tar.xz|s6-overlay-$(S6_VERSION)-x86_64.tar.xz"	\
		"$(S6_URL)/v$(S6_VERSION)/s6-overlay-symlinks-noarch.tar.xz|s6-overlay-$(S6_VERSION)-symlinks-noarch.tar.xz" \
		"$(RIPGREP_URL)/$$7/ripgrep-$$7-$(RIPGREP_TARGET).tar.gz|"									\
		"$(OLM_URL)/$$8/olm-$$8.tar.gz|"															\
	; do url=$${spec%%|*}; name=$${spec#*|}; name=$${name:-$${url##*/}}
		[ -f "$(ARTIFACTS)/src/$$name" ] || curl -fvL -o "$(ARTIFACTS)/src/$$name" "$$url"
	done
	# суммы s6 сверяем сразу: тарболы приходят с github-cdn, битый архив
	# должен падать здесь, а не на середине docker build
	cd $(ARTIFACTS)/src
	printf '%s  %s\n'														\
		"$(S6_SHA_NOARCH)"		"s6-overlay-$(S6_VERSION)-noarch.tar.xz"	\
		"$(S6_SHA_X86_64)"		"s6-overlay-$(S6_VERSION)-x86_64.tar.xz"	\
		"$(S6_SHA_SYMLINKS)"	"s6-overlay-$(S6_VERSION)-symlinks-noarch.tar.xz" \
		| sha256sum -c -


# Показывает версии, зашитые в docker/Dockerfile собираемого тега: s6-overlay,
# sqlite, python. Значения отсюда переносятся руками в переменные Makefile
.PHONY: deps
deps: deps-$(VERSION)

.ONESHELL:
deps-%:
	@current=$(*)
	set -e
	src='$(ARTIFACTS)/src/hermes-agent-'"$$current"'-src.tar.gz'
	[ -f "$$src" ] || { mkdir -p $(ARTIFACTS)/src; curl -fsL -o "$$src" "$(BASE_URL)/v$$current"; }
	root=$$(tar -tzf "$$src" | head -1 | cut -d/ -f1)
	tar -xzOf "$$src" "$$root/Dockerfile" > /tmp/hermes-dockerfile.$$$$
	trap 'rm -f /tmp/hermes-dockerfile.$$$$' EXIT
	echo "hermes-agent $$current (значения из docker/Dockerfile тега):"
	grep -E '^ARG (S6_OVERLAY|SQLITE)' /tmp/hermes-dockerfile.$$$$ | sed 's/^ARG /  /'
	echo ""
	echo "python из pyproject.toml:"
	tar -xzOf "$$src" "$$root/pyproject.toml" | grep -E '^requires-python' | sed 's/^/  /'
	echo ""
	echo "node из package.json:"
	tar -xzOf "$$src" "$$root/package.json" | python3 -c 'import json,sys; print("  node:", (json.load(sys.stdin).get("engines") or {}).get("node","-"))'


# docker build --target dist - дистрибутив с скомпилированными исходниками (без установки в систему)
.PHONY: dist
dist: dist-$(VERSION)

.ONESHELL:
dist-%: sources-%
	@current=$(*)
	set -e
	set -- $(DEPS_$*)
	$(MAKE) --no-print-directory artifacts-link
	docker build -f Dockerfile $(NO_CACHE_TEXT) --target dist	\
		--build-arg RUNTIME_IMAGE=$$1							\
		--build-arg PYTHON_IMAGE=$$2							\
		--build-arg GCC_IMAGE=$$3								\
		--build-arg UV_IMAGE=$$4								\
		--build-arg SQLITE_IMAGE=$$5							\
		--build-arg CMAKE_IMAGE=$$6								\
		--build-arg RIPGREP_VERSION=$$7							\
		--build-arg RIPGREP_TARGET=$(RIPGREP_TARGET)			\
		--build-arg OLM_VERSION=$$8								\
		--build-arg HERMES_VERSION=$$current					\
		--build-arg S6_VERSION=$(S6_VERSION)					\
		--build-arg UV_EXTRAS="$(UV_EXTRAS)"					\
		--build-arg WITH_PLAYWRIGHT=$(WITH_PLAYWRIGHT)			\
		--build-arg PLAYWRIGHT_PLATFORM=$(PLAYWRIGHT_PLATFORM)	\
		--build-arg WITH_CHROMIUM=$(WITH_CHROMIUM)			\
		--tag $(IMAGE):$$current-dist							\
		--progress plain $(BUILD_CONTEXT)


# извлечь собранный дистрибутив (/opt/hermes) из -dist образа
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
	# команда обязательна: у -dist образа нет ни ENTRYPOINT, ни CMD
	cid=$$(docker create $$img /bin/true)
	trap 'docker rm -f $$cid >/dev/null 2>&1 || true' EXIT
	rm -rf "$$dst"
	mkdir -p "$$dst"
	docker cp "$$cid:$(HERMES_PATH)/." "$$dst/"
	echo "extracted $$img:$(HERMES_PATH) -> $$dst"
	du -h --max-depth=1 "$$dst" || true


# docker build - истоговый образ с установленным ПО (установка из дистрибутива)
.PHONY: build
build: build-$(VERSION)

.ONESHELL:
build-%: dist-%
	@current=$(*)
	set -e
	set -- $(DEPS_$*)
	major=$${current%.*}
	$(MAKE) --no-print-directory artifacts-link
	docker build -f Dockerfile $(NO_CACHE_TEXT)		\
		--build-arg RUNTIME_IMAGE=$$1				\
		--build-arg PYTHON_IMAGE=$$2				\
		--build-arg GCC_IMAGE=$$3					\
		--build-arg UV_IMAGE=$$4					\
		--build-arg SQLITE_IMAGE=$$5				\
		--build-arg CMAKE_IMAGE=$$6					\
		--build-arg RIPGREP_VERSION=$$7				\
		--build-arg RIPGREP_TARGET=$(RIPGREP_TARGET)	\
		--build-arg OLM_VERSION=$$8					\
		--build-arg HERMES_VERSION=$$current		\
		--build-arg S6_VERSION=$(S6_VERSION)		\
		--build-arg UV_EXTRAS="$(UV_EXTRAS)"		\
		--build-arg WITH_PLAYWRIGHT=$(WITH_PLAYWRIGHT)			\
		--build-arg PLAYWRIGHT_PLATFORM=$(PLAYWRIGHT_PLATFORM)	\
		--build-arg WITH_CHROMIUM=$(WITH_CHROMIUM)	\
		--tag $(IMAGE):$$current					\
		--tag $(IMAGE):$$major						\
		--progress plain $(BUILD_CONTEXT)


# тестирование собранного образа
.PHONY: test
test: test-$(VERSION)

.ONESHELL:
test-%: build-%
	@current=$(*)
	set -e
	docker run --rm --entrypoint /bin/bash -e HERMES_HOME=/tmp/hermes-home \
		-v $(TEST_DIR):/test:ro $(IMAGE):$$current -euc '\
		echo "== versions =="; 						\
		node --version; 							\
		python3 --version; 							\
		hermes --version; 							\
		git --version; 								\
		rg --version | head -1; 					\
		echo "== sqlite =="; 						\
		python3 /test/sqlite_check.py; 				\
		echo "== olm =="; 							\
		ls -l /usr/local/lib/libolm.so*; 			\
		python3 /test/olm_check.py; 				\
		echo "== s6 =="; 							\
		test -x /init; 								\
		ls /etc/s6-overlay/s6-rc.d; 				\
		echo "== venv =="; 							\
		test -x /opt/hermes/.venv/bin/hermes; 		\
		python3 -c "import agent, gateway, hermes_cli; print(\"  ok    пакеты hermes импортируются\")"; \
		echo "== web/tui бандлы =="; 				\
		test -f /opt/hermes/hermes_cli/web_dist/index.html; \
		echo "  ok    dashboard bundle"; 			\
		test -f /opt/hermes/ui-tui/dist/entry.js; 	\
		echo "  ok    tui bundle"; 					\
		echo "== api server =="; 					\
		python3 /test/api_server_check.py; 			\
		echo "== browser =="; 						\
		agent-browser --version; 					\
		"$$AGENT_BROWSER_EXECUTABLE_PATH" --version; \
		python3 /test/browser_check.py'
	# штатный запуск: /init поднимает supervision tree, отрабатывают cont-init.d
	# хуки (stage2-hook, регистрация профилей), затем стартуют сервисы
	@echo "== s6 boot =="
	@docker run --rm -e HERMES_UID=$$(id -u) -e HERMES_GID=$$(id -g) \
		$(IMAGE):$(*) --version 2>&1 | grep -E "reconcile:|successfully started|Hermes Agent v" | sed 's/^/  /'


.PHONY: clean
clean: clean-$(VERSION)
	rm -rf "$(LINKED_ARTIFACTS)"

.ONESHELL:
clean-%:
	@current=$(*)
	set -e
	set -- $(DEPS_$*)
	for spec in $$v																	\
		"$(ARTIFACTS)/src/hermes-agent-$$current-src.tar.gz"						\
		"$(ARTIFACTS)/src/s6-overlay-$(S6_VERSION)-noarch.tar.xz"					\
		"$(ARTIFACTS)/src/s6-overlay-$(S6_VERSION)-x86_64.tar.xz"					\
		"$(ARTIFACTS)/src/s6-overlay-$(S6_VERSION)-symlinks-noarch.tar.xz"			\
		"$(ARTIFACTS)/src/ripgrep-$$7-$(RIPGREP_TARGET).tar.gz"						\
		"$(ARTIFACTS)/src/olm-$$8.tar.gz"											\
	; do rm -f "$$spec" ;
	done


.PHONY: all
all: sources dist build test
