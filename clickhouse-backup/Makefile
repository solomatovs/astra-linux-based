BASE_DIR      	?= $(abspath .)
ARTIFACTS     	?= $(abspath $(BASE_DIR)/../../ci-artifacts/clickhouse-backup)
BUILD_CONTEXT 	?= $(BASE_DIR)
# рабочая копия $(ARTIFACTS) внутри контекста сборки (хардлинки, см. artifacts-link)
LINKED_ARTIFACTS ?= $(BUILD_CONTEXT)/artifacts
TEST_DIR		?= $(BASE_DIR)/test

# путь, по которому дистрибутив лежит внутри -dist образа (CB_PATH в Dockerfile)
CB_PATH			?= /opt/clickhouse-backup


.DEFAULT_GOAL := help

# список версий, которые поддерживает сборщик
VERSION_CHAIN		?= 2.6.44 2.8.0
# целевая версия сборки. по умолчанию берется последняя версия
VERSION      		?= $(lastword $(VERSION_CHAIN))
# базовый url адрес для скачивания исходников собираемых в Dockerfile
BASE_URL			?= https://codeload.github.com/Altinity/clickhouse-backup/tar.gz/refs/tags
# go env для закрытого контура (nexus), подкладывается в /etc/go/env на этапе
# sources. по умолчанию файла нет — go идет в публичный proxy.golang.org.
# образец: go-env.example
GO_ENV				?= $(BASE_DIR)/go-env
# выполнять ли сброс кэша: docker build --no-cache
NO_CACHE			?= 0

GLIBC_IMAGE			?= dmp/glibc:2.28
# образ clickhouse, на котором прогоняется make test
CH_IMAGE			?= dmp/clickhouse:25.12.12.1-stable

# зависимости для сборки разных версии
# go_image: версия не ниже директивы go в go.mod тега, иначе go полезет качать
# тулчейн. узнать значение для новой версии: make deps VERSION=<версия>
# версия           runtime_image	go_image
DEPS_2.6.44		:= $(GLIBC_IMAGE)	dmp/golang:1.25.12-dist
DEPS_2.8.0		:= $(GLIBC_IMAGE)	dmp/golang:1.26.5-dist

# базовое название для образа
IMAGE			:= dmp/clickhouse-backup
FIRST_VERSION	:= $(firstword $(VERSION_CHAIN))
NO_CACHE_TEXT	:= $(if $(filter 1,$(NO_CACHE)),--no-cache,)


# helper: поиск предыдущей версии в VERSION_CHAIN
prev_of = $(shell echo "$(VERSION_CHAIN)" | awk -v v='$(1)' '{for(i=1;i<=NF;i++){if($$i==v){print p;exit};p=$$i}}')


.PHONY: help
help:
	@echo "make:"
	@echo "  make all        собрать всё"
	@echo "  make sources    скачать исходники и вендорить go-модули для цепочки вплоть до $(VERSION)"
	@echo "  make deps       показать версию go, которую требуют исходники $(VERSION) (для переноса в DEPS)"
	@echo "  make dist       собрать цепочку -dist образов одну за другой до $(VERSION)"
	@echo "  make extract    достать дистрибутив ($(CB_PATH)) из -dist образа в $(ARTIFACTS)/build/<версия>"
	@echo "  make build      собрать образ $(IMAGE)"
	@echo "  make test       поднять $(CH_IMAGE) и прогнать create/restore рядом с ним"
	@echo ""
	@echo "  Цепочка: $(VERSION_CHAIN)"
	@echo "  Altinity clickhouse-backup: бэкап/восстановление ClickHouse с выгрузкой"
	@echo "  в объектное хранилище (S3/GCS/Azure/FTP/SFTP), инкрементальные копии."
	@echo "  Работает рядом с сервером: нужен доступ к тому же /var/lib/clickhouse."
	@echo "  vendor/ в апстриме нет, поэтому make sources вендорит go-модули в"
	@echo "  $(ARTIFACTS)/src, а сборка идет с GOFLAGS=-mod=vendor GOPROXY=off."
	@echo "  Nexus: go-env.example -> go-env, подкладывается в /etc/go/env на этапе sources."
	@echo "  2.7+ требует go 1.26, 2.6.x собирается на go 1.25."
	@echo "  Зависимости задаются пословно в DEPS_<версия>."
	@echo "  Другая версия: make all VERSION=2.6.44"
	@echo ""


.PHONY: debug
debug:
	$(info BASE_DIR       = $(BASE_DIR))
	$(info ARTIFACTS      = $(ARTIFACTS))
	$(info CB_PATH        = $(CB_PATH))
	$(info BUILD_CONTEXT  = $(BUILD_CONTEXT))
	$(info LINKED_ARTIFACTS = $(LINKED_ARTIFACTS))
	$(info TEST_DIR       = $(TEST_DIR))
	$(info VERSION_CHAIN  = $(VERSION_CHAIN))
	$(info VERSION        = $(VERSION))
	$(info FIRST_VERSION  = $(FIRST_VERSION))
	$(info BASE_URL       = $(BASE_URL))
	$(info GO_ENV         = $(GO_ENV))
	$(info CH_IMAGE       = $(CH_IMAGE))
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
	@prev='$(call prev_of,$*)'
	# вызываем рекурсивно предыдущую версию
	[ -n "$$prev" ] && $(MAKE) sources-$$prev || true
	set -e
	set -- $(DEPS_$*)
	mkdir -p $(ARTIFACTS)/src
	src="$(ARTIFACTS)/src/clickhouse-backup-$$current-src.tar.gz"
	[ -f "$$src" ] || curl -fvL -o "$$src" "$(BASE_URL)/v$$current"
	# go-модули — единственное, что сборка тянула бы из сети. вендорим заранее тем
	# же тулчейном, которым потом собираем: версия go влияет на состав vendor/
	vendor="$(ARTIFACTS)/src/vendor-$$current.tar.gz"
	if [ ! -f "$$vendor" ]; then
		tmp=$$(mktemp -d)
		trap 'rm -rf "$$tmp"' EXIT
		tar -xzf "$$src" -C "$$tmp" --strip-components=1
		# в -dist образе /etc/go/env нет (его кладет только runtime-стадия
		# dmp/golang), поэтому конфиг закрытого контура монтируем сами
		goenv=""
		if [ -f "$(GO_ENV)" ]; then
			goenv="-e GOENV=/etc/go/env -v $(GO_ENV):/etc/go/env:ro"
		fi
		# в -dist образе тулчейн лежит деревом /opt/go/usr/local/go, запускаем
		# go оттуда напрямую. --user: иначе vendor/ создается от root
		docker run --rm													\
			--user "$$(id -u):$$(id -g)"								\
			-v "$$tmp:/src" -w /src										\
			$$goenv														\
			-e GOROOT=/opt/go/usr/local/go								\
			-e GOFLAGS=-mod=mod											\
			-e GOTOOLCHAIN=local										\
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
	src="$(ARTIFACTS)/src/clickhouse-backup-$$current-src.tar.gz"
	[ -f "$$src" ] || curl -fsL -o "$$src" "$(BASE_URL)/v$$current"
	gomod=$$(tar -xzOf "$$src" "clickhouse-backup-$$current/go.mod")
	go_directive=$$(echo "$$gomod" | sed -n 's/^go \(.*\)$$/\1/p')
	toolchain=$$(echo "$$gomod" | sed -n 's/^toolchain go\(.*\)$$/\1/p')
	echo "clickhouse-backup $$current:"
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
dist-%: sources-%
	@current=$(*)
	prev='$(call prev_of,$*)'
	# рекурсивно собираем предыдущую версию
	[ -n "$$prev" ] && $(MAKE) dist-$$prev || true
	set -e
	set -- $(DEPS_$*)
	$(MAKE) --no-print-directory artifacts-link
	docker build -f Dockerfile $(NO_CACHE_TEXT) --target dist	\
		--build-arg RUNTIME_IMAGE=$$1							\
		--build-arg GO_IMAGE=$$2								\
		--build-arg CB_VERSION=$$current						\
		--tag $(IMAGE):$$current-dist							\
		--progress plain $(BUILD_CONTEXT)


# извлечь скомпилированный дистрибутив (/opt/clickhouse-backup) из -dist образа
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
	docker cp "$$cid:$(CB_PATH)/." "$$dst/"
	echo "extracted $$img:$(CB_PATH) -> $$dst"
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
	$(MAKE) --no-print-directory artifacts-link
	docker build -f Dockerfile $(NO_CACHE_TEXT)					\
		--build-arg RUNTIME_IMAGE=$$1							\
		--build-arg GO_IMAGE=$$2								\
		--build-arg CB_VERSION=$$current						\
		--tag $(IMAGE):$$current								\
		--tag $(IMAGE):$$major									\
		--progress plain $(BUILD_CONTEXT)


# тестирование собранного образа: clickhouse-backup работает только рядом с
# сервером, поэтому поднимаем $(CH_IMAGE) и цепляем к нему бэкапер по
# --volumes-from (тот же /var/lib/clickhouse) и --network container: (тот же localhost:9000)
.PHONY: test
test: test-$(VERSION)

.ONESHELL:
test-%: build-%
	@current=$(*)
	set -e
	name="cb-test-$$$$"
	docker volume create "$$name-data" >/dev/null
	trap 'docker rm -f "$$name" >/dev/null 2>&1 || true; docker volume rm -f "$$name-data" >/dev/null 2>&1 || true' EXIT
	docker run -d --name "$$name"					\
		-v "$$name-data:/var/lib/clickhouse"		\
		$(CH_IMAGE) server							\
		--config-file=/usr/local/etc/clickhouse-server/config.xml >/dev/null
	echo "== ожидание сервера =="
	ready=0
	for i in $$(seq 60); do
		docker exec "$$name" clickhouse client --query "SELECT 1" >/dev/null 2>&1 && ready=1 && break
		sleep 1
	done
	[ "$$ready" = 1 ]
	echo "  ok    clickhouse принимает запросы"
	echo "== наполнение данными =="
	docker exec -i "$$name" clickhouse client --multiquery < $(TEST_DIR)/smoke.sql
	before=$$(docker exec "$$name" clickhouse client --query "SELECT count() FROM smoke.events")
	echo "  строк до бэкапа: $$before"
	backup="docker run --rm --volumes-from $$name --network container:$$name -v $(TEST_DIR)/config.yml:/etc/clickhouse-backup/config.yml:ro $(IMAGE):$$current"
	echo "== version =="
	$$backup --version
	$$backup --version | grep -q "$$current"
	echo "  ok    бинарник знает свою версию (ldflags main.version)"
	echo "== print-config =="
	$$backup print-config | head -20
	echo "== create =="
	$$backup create smoke
	$$backup tables
	echo "== list local =="
	$$backup list local
	$$backup list local | grep -q smoke
	echo "  ok    бэкап в списке"
	echo "== удаление таблицы =="
	docker exec "$$name" clickhouse client --query "DROP TABLE smoke.events SYNC"
	echo "== restore =="
	$$backup restore smoke
	after=$$(docker exec "$$name" clickhouse client --query "SELECT count() FROM smoke.events")
	echo "  строк после restore: $$after"
	[ "$$before" = "$$after" ]
	echo "  ok    данные восстановлены"
	echo "== delete =="
	$$backup delete local smoke
	$$backup list local | grep -q smoke && exit 1 || true
	echo "  ok    бэкап удален"


.PHONY: clean
clean: clean-$(VERSION)
	rm -rf "$(LINKED_ARTIFACTS)"

.ONESHELL:
clean-%:
	@current=$(*)
	@prev='$(call prev_of,$*)'
	# вызываем рекурсивно предыдущую версию
	[ -n "$$prev" ] && $(MAKE) clean-$$prev || true
	set -e
	rm -f "$(ARTIFACTS)/src/clickhouse-backup-$$current-src.tar.gz"
	rm -f "$(ARTIFACTS)/src/vendor-$$current.tar.gz"


.PHONY: all
all: sources dist build test
