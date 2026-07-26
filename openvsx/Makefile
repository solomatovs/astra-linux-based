BASE_DIR      	?= $(abspath .)
ARTIFACTS     	?= $(BASE_DIR)/artifacts
BUILD_CONTEXT 	?= $(BASE_DIR)

# путь, по которому дистрибутив лежит внутри -dist образа (OVSX_PATH в Dockerfile)
OVSX_PATH		?= /opt/openvsx


.DEFAULT_GOAL := help

# список версий, которые поддерживает сборщик
# v0.30.0 — последняя на java 17; с v0.31.0 апстрим требует java 25 и gradle 9,
# для них сначала придется достроить цепочку в соседнем проекте java
VERSION_CHAIN		?= 0.30.0
# целевая версия сборки. по умолчанию берется последняя версия
VERSION      		?= $(lastword $(VERSION_CHAIN))
# базовый url адрес для скачивания исходников собираемых в Dockerfile
BASE_URL			?= https://codeload.github.com/eclipse-openvsx/openvsx/tar.gz/refs/tags
# выполнять ли сброс кэша: docker build --no-cache
NO_CACHE			?= 0

JAVA_IMAGE			?= dmp/java:17
NODE_IMAGE			?= dmp/nodejs:22.22.1-dist
GCC_IMAGE			?= dmp/gcc:12.4.0-dist
# postgres нужен только для make test: сам образ его не содержит
POSTGRES_IMAGE		?= dmp/postgres:16

# зависимости для сборки разных версии
# java_image:  jdk + gradle/maven (см. проект java), им собирается server
# node_image:  -dist с node для сборки webui (yarn вендорен в исходниках openvsx)
# gcc_image:   libstdc++ для node — системная в базовом образе старее
# узнать значения для новой версии: make deps VERSION=<версия>
# версия           runtime_image	build_image		node_image		gcc_image
DEPS_0.30.0		:= $(JAVA_IMAGE)	$(JAVA_IMAGE)	$(NODE_IMAGE)	$(GCC_IMAGE)

# базовое название для образа
IMAGE			:= dmp/openvsx
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
	@echo "  make extract    достать дистрибутив ($(OVSX_PATH)) из -dist образа в $(ARTIFACTS)/build/<версия>"
	@echo "  make build      собрать образ $(IMAGE)"
	@echo "  make test       проверить образ $(IMAGE): поднять postgres+server, дождаться health, спросить search"
	@echo ""
	@echo "  Цепочка: $(VERSION_CHAIN)"
	@echo "  webui собирается yarn-ом (вендорен в исходниках) и запекается внутрь"
	@echo "  jar-ника, server — системным gradle из $(JAVA_IMAGE) (не wrapper-ом:"
	@echo "  тот качает дистрибутив gradle с services.gradle.org)."
	@echo "  Рантайму нужен только postgres: поиск переведен на базу (без elasticsearch),"
	@echo "  сессии в jdbc, jobrunr в sql — см. config/application.yml."
	@echo "  Все параметры переопределяются в рантайме: SPRING_*/OVSX_* переменными"
	@echo "  окружения или монтированием своего application.yml, ничего не запекается."
	@echo "  Зависимости (runtime/build/node/gcc) задаются пословно в DEPS_<версия>."
	@echo ""


.PHONY: debug
debug:
	$(info BASE_DIR       = $(BASE_DIR))
	$(info ARTIFACTS      = $(ARTIFACTS))
	$(info OVSX_PATH      = $(OVSX_PATH))
	$(info BUILD_CONTEXT  = $(BUILD_CONTEXT))
	$(info VERSION_CHAIN  = $(VERSION_CHAIN))
	$(info VERSION        = $(VERSION))
	$(info FIRST_VERSION  = $(FIRST_VERSION))
	$(info BASE_URL       = $(BASE_URL))
	$(info NO_CACHE       = $(NO_CACHE))
	$(info NO_CACHE_TEXT  = $(NO_CACHE_TEXT))
	$(info POSTGRES_IMAGE = $(POSTGRES_IMAGE))
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
	[ -f "$(ARTIFACTS)/src/openvsx-$$current.tar.gz" ] || curl -fvL -o "$(ARTIFACTS)/src/openvsx-$$current.tar.gz" "$(BASE_URL)/v$$current"


# Показывает версии зависимостей, которые требуют исходники указанной версии:
# java и gradle зашиты в build-файлах сервера, yarn вендорен в webui.
# Числа отсюда переносятся руками в DEPS_<версия>. Работает и для версии,
# которой еще нет в DEPS — тарбол качает сам
.PHONY: deps
deps: deps-$(VERSION)

.ONESHELL:
deps-%:
	@current=$(*)
	set -e
	mkdir -p $(ARTIFACTS)/src
	src='$(ARTIFACTS)/src/openvsx-'"$$current"'.tar.gz'
	[ -f "$$src" ] || curl -fsL -o "$$src" "$(BASE_URL)/v$$current"
	java=$$(tar -xzOf $$src openvsx-$$current/server/build.gradle | sed -n "s/^ *java: '\(.*\)',/\1/p")
	[ -n "$$java" ] || java=$$(tar -xzOf $$src openvsx-$$current/server/gradle/libs.versions.toml 2>/dev/null | sed -n 's/^java *= *"\(.*\)"/\1/p')
	echo "openvsx $$current:"
	echo "  java:          $$java  -> нужен образ dmp/java:$$java"
	echo "  gradle:        $$(tar -xzOf $$src openvsx-$$current/server/gradle/wrapper/gradle-wrapper.properties | grep -oE 'gradle-[0-9.]+' | head -1 | cut -d- -f2)  -> собирается системным gradle из dmp/java, версия должна быть не ниже"
	echo "  yarn:          $$(tar -tzf $$src | grep -oE 'yarn-[0-9.]+\.cjs' | head -1)  -> вендорен в исходниках, качать не надо"
	echo "  node (webui):  $$(tar -xzOf $$src openvsx-$$current/webui/package.json | python3 -c 'import json,sys; print((json.load(sys.stdin).get("engines") or {}).get("node","не задано"))')"


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
		--build-arg BUILD_IMAGE=$$2									\
		--build-arg NODE_IMAGE=$$3									\
		--build-arg GCC_IMAGE=$$4									\
		--build-arg OVSX_VERSION=$$current							\
		--tag $(IMAGE):$$current-dist								\
		--progress plain $(BUILD_CONTEXT)


# извлечь скомпилированный дистрибутив (/opt/openvsx) из -dist образа
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
	docker cp "$$cid:$(OVSX_PATH)/." "$$dst/"
	echo "extracted $$img:$(OVSX_PATH) -> $$dst"
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
		--build-arg BUILD_IMAGE=$$2									\
		--build-arg NODE_IMAGE=$$3									\
		--build-arg GCC_IMAGE=$$4									\
		--build-arg OVSX_VERSION=$$current							\
		--tag $(IMAGE):$$current									\
		--progress plain $(BUILD_CONTEXT)


# тестирование собранного образа: postgres + server в одной сети,
# ждем health и спрашиваем поиск (работает по базе, без elasticsearch)
.PHONY: test
test: test-$(VERSION)

.ONESHELL:
test-%: build-%
	@current=$(*)
	set -e
	trap 'docker rm -f ovsx-test-db ovsx-test-sv >/dev/null 2>&1 || true; docker network rm ovsx-test >/dev/null 2>&1 || true' EXIT
	docker network create ovsx-test >/dev/null
	docker run -d --name ovsx-test-db --network ovsx-test \
		-e POSTGRES_USER=openvsx -e POSTGRES_PASSWORD=openvsx $(POSTGRES_IMAGE) >/dev/null
	echo "== ждем postgres =="
	for i in $$(seq 1 30); do \
		docker exec -e PGPASSWORD=openvsx ovsx-test-db psql -h 127.0.0.1 -U openvsx -d postgres -c 'select 1' >/dev/null 2>&1 && break; sleep 1; done
	docker exec -e PGPASSWORD=openvsx ovsx-test-db psql -h 127.0.0.1 -U openvsx -d postgres -c 'CREATE DATABASE openvsx' >/dev/null
	docker run -d --name ovsx-test-sv --network ovsx-test -p 127.0.0.1:18080:8080 \
		-e SPRING_DATASOURCE_URL=jdbc:postgresql://ovsx-test-db:5432/openvsx \
		$(IMAGE):$$current >/dev/null
	echo "== ждем server (flyway накатывает схему при первом старте) =="
	for i in $$(seq 1 90); do \
		curl -fsS http://127.0.0.1:18080/actuator/health 2>/dev/null | grep -q '"UP"' && break; sleep 2; done
	echo "== health =="
	curl -fsS http://127.0.0.1:18080/actuator/health; echo
	echo "== search (databasesearch, пустой реестр -> 0 расширений) =="
	curl -fsS 'http://127.0.0.1:18080/api/-/search?size=1'; echo
	echo "== webui отдается =="
	curl -fsS http://127.0.0.1:18080/ | grep -qi '<script' && echo "  ok    index.html с бандлом"
	echo "== версия реестра =="
	curl -fsS http://127.0.0.1:18080/api/version 2>/dev/null || docker exec ovsx-test-sv sh -c 'grep -o "version: .*" config/application.yml | head -1'


.PHONY: clean
clean: clean-$(VERSION)

.ONESHELL:
clean-%:
	@current=$(*)
	@prev='$(call prev_of,$*)'
	# вызываем рекурсивно предыдущую версию
	[ -n "$$prev" ] && $(MAKE) clean-$$prev || true
	set -e
	rm -f "$(ARTIFACTS)/src/openvsx-$$current.tar.gz"


.PHONY: all
all: sources dist build test
