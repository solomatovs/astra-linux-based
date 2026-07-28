BASE_DIR      	?= $(abspath .)
ARTIFACTS     	?= $(abspath $(BASE_DIR)/../../ci-artifacts/poetry)
BUILD_CONTEXT 	?= $(BASE_DIR)
# рабочая копия $(ARTIFACTS) внутри контекста сборки (хардлинки, см. artifacts-link)
LINKED_ARTIFACTS ?= $(BUILD_CONTEXT)/artifacts
TEST_DIR		?= $(BASE_DIR)/test


.DEFAULT_GOAL := help

# список версий, которые поддерживает сборщик
# версии собираются одна за другой, строго по цепочке
VERSION_CHAIN		?= 1.8.5 2.1.4 2.4.1
# целевая версия сборки. по умолчанию берется последняя версия
VERSION      		?= $(lastword $(VERSION_CHAIN))
# индекс, из которого качаются колёса (шаг sources — единственный шаг с сетью).
# пусто = не передавать --index-url вообще: тогда решает pip.conf (см. PIP_CONF),
# а если и его нет — pypi.org. Значение в командной строке перебивает конфиг,
# поэтому по умолчанию здесь пусто, а не https://pypi.org/simple
INDEX_URL			?=
# конфиг pip для шага sources: index-url, trusted-host, proxy, сертификаты.
# реальный файл — pip.conf рядом с Makefile (он в .gitignore), см. pip.conf.example.
# монтируется в контейнер как /etc/pip.conf, если существует
PIP_CONF			?= $(BASE_DIR)/pip.conf
# базовый url адрес для чтения метаданных релиза (только для make deps)
PYPI_JSON_URL		?= https://pypi.org/pypi
# выполнять ли сброс кэша: docker build --no-cache
NO_CACHE			?= 0

# рантайм и сборка — ОДИН И ТОТ ЖЕ образ python, и разъехаться им нельзя:
#  - poetry ставится в venv, а в venv зашиты абсолютные пути до /usr/local/bin/python3;
#  - колёса с C-расширениями (cryptography, rapidfuzz, dulwich, msgpack) собраны
#    под конкретный ABI, cp313 в cp311 не поедет.
# поэтому DEPS_<версия> — одно слово, а не пара runtime/build, как у остальных проектов
PYTHON_IMAGE		?= dmp/python:3.13
PYTHON_LEGACY_IMAGE	?= dmp/python:3.11

# что собирать из sdist, а не брать готовым колесом (шаг sources, сеть есть).
# сам poetry и poetry-core — чистый python на poetry-core, собираются за секунды
# и дают настоящую сборку из исходников. Остальное дерево берётся колёсами
# manylinux: собирать его из sdist значит затащить в контур rust (cryptography)
# и C++17 (rapidfuzz), чего в тулчейне gcc-8.5.0 нет.
# полностью из исходников: NO_BINARY=:all: (нужен свой build-образ с cc и rustc)
NO_BINARY			?= poetry,poetry-core

# плагины, которые ставятся в тот же venv (оффлайн-аналог `poetry self add`).
# export нужен в закрытом контуре: poetry export -f requirements.txt
PLUGINS				?= poetry-plugin-export

# зависимости для сборки разных версии
# версия           python_image (он же runtime, он же build)
DEPS_1.8.5		:= $(PYTHON_LEGACY_IMAGE)
DEPS_2.1.4		:= $(PYTHON_IMAGE)
DEPS_2.4.1		:= $(PYTHON_IMAGE)

# базовое название для образа
IMAGE			:= dmp/poetry
FIRST_VERSION	:= $(firstword $(VERSION_CHAIN))
NO_CACHE_TEXT	:= $(if $(filter 1,$(NO_CACHE)),--no-cache,)


# helper: поиск предыдущей версии в VERSION_CHAIN
prev_of = $(shell echo "$(VERSION_CHAIN)" | awk -v v='$(1)' '{for(i=1;i<=NF;i++){if($$i==v){print p;exit};p=$$i}}')


.PHONY: help
help:
	@echo "make:"
	@echo "  make all        собрать всё"
	@echo "  make deps       сверить requires-python версии $(VERSION) с python из DEPS (нужна сеть)"
	@echo "  make sources    завендорить колёса для цепочки вплоть до $(VERSION)"
	@echo "  make dist       собрать цепочку -dist образов одну за другой до $(VERSION)"
	@echo "  make build      собрать образ $(IMAGE)"
	@echo "  make test       проверить образ $(IMAGE) с ВЫКЛЮЧЕННОЙ сетью (--network none)"
	@echo "  make wheels     показать пины всех колёс, из которых собран образ"
	@echo "  make glibc      показать минимальный glibc, который требуют колёса образа"
	@echo ""
	@echo "  Цепочка: $(VERSION_CHAIN)"
	@echo "  poetry — чистый python, «компиляция» здесь это вендоринг колёс:"
	@echo "  sources качает всё дерево зависимостей (pip wheel, сеть) в"
	@echo "  $(ARTIFACTS)/src/poetry-<версия>-python<тег>.tar.gz, а dist/build ставят"
	@echo "  его в venv через pip install --no-index (без сети)."
	@echo "  Из исходников (sdist) собирается NO_BINARY=$(NO_BINARY)."
	@echo "  Плагины в тот же venv: PLUGINS=$(PLUGINS)"
	@echo "  Образ сборки = образ рантайма, задаётся одним словом в DEPS_<версия>."
	@echo "  Индекс для sources: pip.conf рядом с Makefile (см. pip.conf.example)"
	@echo "  или INDEX_URL=... (аргумент перебивает конфиг, задавать что-то одно)."
	@echo "  Другая версия: make all VERSION=1.8.5"
	@echo "  Другой python: make all PYTHON_IMAGE=dmp/python:3.12"
	@echo ""


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
	$(info INDEX_URL      = $(INDEX_URL))
	$(info PIP_CONF       = $(PIP_CONF) $(if $(wildcard $(PIP_CONF)),(есть),(нет — pip ходит в pypi.org)))
	$(info PYPI_JSON_URL  = $(PYPI_JSON_URL))
	$(info PYTHON_IMAGE   = $(PYTHON_IMAGE))
	$(info NO_BINARY      = $(NO_BINARY))
	$(info PLUGINS        = $(PLUGINS))
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


# Сверяет requires-python релиза с python из DEPS_<версия>: ограничение объявлено
# в метаданных на pypi, а нарушение вылезает не при установке колеса, а раньше —
# на шаге sources, когда pip молча резолвит СТАРУЮ версию poetry под этот python.
# Проверять поэтому нужно ДО вендоринга.
.PHONY: deps
deps: deps-$(VERSION)

.ONESHELL:
deps-%:
	@current=$(*)
	set -e
	set -- $(DEPS_$*)
	# pip.conf здесь не при чём: check-python.py ходит на pypi через urllib,
	# в закрытом контуре зеркало задаётся PYPI_JSON_URL
	docker run --rm -v "$(BASE_DIR)/check-python.py":/check-python.py:ro \
		--entrypoint python3 $$1 /check-python.py "$(PYPI_JSON_URL)" poetry "$$current" "$$1"


# Вендорим колёса для оффлайн-сборки: всё дерево зависимостей (poetry + PLUGINS)
# складывается в тарбол вместе с install.txt (что ставить) и constraints.txt
# (пины всего, что приехало). Единственный шаг, которому нужна сеть.
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
	pytag=$${1##*:}
	mkdir -p $(ARTIFACTS)/src
	# уже завендорено — пропускаем
	[ -f "$(ARTIFACTS)/src/poetry-$$current-python$$pytag.tar.gz" ] && exit 0 || true
	# конфиг индекса приезжает файлом, а не переменными окружения: pip.conf умеет
	# то, чего одним --index-url не скажешь (trusted-host, cert, proxy, креды),
	# и это ровно тот же файл, что кладётся в python-образ соседним проектом
	pipconf=""
	if [ -f "$(PIP_CONF)" ]; then pipconf="-v $(PIP_CONF):/etc/pip.conf:ro"; echo "pip.conf: $(PIP_CONF)"; fi
	# колёса резолвятся ТЕМ ЖЕ интерпретатором, который будет в рантайме:
	# у cp313/cp311 разные ABI-теги, чужие колёса просто не поставятся
	docker run --rm										\
		-e V="$$current" -e PYTAG="$$pytag"				\
		-e PLUGINS="$(PLUGINS)" -e NO_BINARY="$(NO_BINARY)"	\
		-e INDEX_URL="$(INDEX_URL)"						\
		$$pipconf										\
		-v "$(ARTIFACTS)/src":/out --entrypoint sh $$1 -c '
			set -eu
			export PIP_ROOT_USER_ACTION=ignore PIP_DISABLE_PIP_VERSION_CHECK=1
			mkdir -p /tmp/wheelhouse
			: > /tmp/wheelhouse/install.txt
			for spec in "poetry==$$V" $$PLUGINS; do echo "$$spec" >> /tmp/wheelhouse/install.txt; done
			cat /tmp/wheelhouse/install.txt
			# --index-url только если INDEX_URL задан: аргумент командной строки
			# старше /etc/pip.conf, пустой перетёр бы настройку из конфига
			pip wheel $${INDEX_URL:+--index-url "$$INDEX_URL"}				\
				--wheel-dir /tmp/wheelhouse									\
				$${NO_BINARY:+--no-binary "$$NO_BINARY"}					\
				-r /tmp/wheelhouse/install.txt
			# пины из имён колёс: в тарболе лежит ровно одна версия каждого пакета,
			# constraints.txt фиксирует это и для установки, и для аудита
			for w in /tmp/wheelhouse/*.whl; do
				b=$$(basename "$$w"); rest=$${b#*-}
				echo "$${b%%-*}==$${rest%%-*}"
			done | sort > /tmp/wheelhouse/constraints.txt
			cat /tmp/wheelhouse/constraints.txt
			du -sh /tmp/wheelhouse
			tar -C /tmp -czf /out/poetry-$$V-python$$PYTAG.tar.gz wheelhouse'
	ls -lh "$(ARTIFACTS)/src/poetry-$$current-python$$pytag.tar.gz"


# docker build --target dist - дистрибутив с установленным poetry (venv в /opt/poetry)
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
	$(MAKE) --no-print-directory artifacts-link
	docker build -f Dockerfile $(NO_CACHE_TEXT) --target dist \
		--build-arg RUNTIME_IMAGE=$$1				\
		--build-arg POETRY_VERSION=$$current		\
		--build-arg PYTHON_TAG=$${1##*:}			\
		--tag $(IMAGE):$$current-dist				\
		--tag $(IMAGE):$$major-dist					\
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
	$(MAKE) --no-print-directory artifacts-link
	docker build -f Dockerfile $(NO_CACHE_TEXT) \
		--build-arg RUNTIME_IMAGE=$$1				\
		--build-arg POETRY_VERSION=$$current		\
		--build-arg PYTHON_TAG=$${1##*:}			\
		--tag $(IMAGE):$$current					\
		--tag $(IMAGE):$$major						\
		--progress plain $(BUILD_CONTEXT)


# пины всех колёс, из которых собран образ (тот самый constraints.txt)
.PHONY: wheels
wheels: wheels-$(VERSION)

.ONESHELL:
wheels-%:
	@current=$(*)
	docker run --rm --entrypoint /bin/cat $(IMAGE):$$current /usr/local/share/poetry/wheels.txt


# порог переносимости: минимальный glibc, при котором заработают колёса образа.
# считается на этапе builder по manylinux-тегам колёс (см. glibc-floor.py) и там же
# сверяется с glibc рантайма — в образе лежит уже готовым файлом
.PHONY: glibc
glibc: glibc-$(VERSION)

.ONESHELL:
glibc-%:
	@current=$(*)
	docker run --rm --entrypoint /bin/cat $(IMAGE):$$current /usr/local/share/poetry/glibc-floor.txt


# тестирование собранного образа.
# --network none: оффлайн-пригодность это главное свойство образа, поэтому тест
# идёт с физически выключенной сетью, а не «на честном слове»
.PHONY: test
test: test-$(VERSION)

.ONESHELL:
test-%: build-%
	@current=$(*)
	docker run --rm --network none --entrypoint /bin/bash -v $(TEST_DIR):/test:ro $(IMAGE):$$current -euc '\
		echo "== versions ==";								\
		poetry --version;									\
		python3 --version;									\
		readlink -f /usr/local/bin/poetry;					\
		echo "== plugins ==";								\
		poetry self show plugins 2>&1 | head -20 || true;	\
		echo "== config ==";								\
		poetry config --list;								\
		echo "== glibc floor ==";							\
		cat /usr/local/share/poetry/glibc-floor.txt;		\
		echo "== демо-проект без сети ==";					\
		cp -r /test/demo /tmp/demo && cd /tmp/demo;			\
		poetry check;										\
		poetry lock;										\
		poetry build;										\
		ls -l dist;											\
		poetry install --no-root;							\
		poetry env info --path;								\
		poetry export -f requirements.txt --output /tmp/req.txt 2>/dev/null && echo "export: ok" || echo "export: плагина нет"; \
		echo "OK"'


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
	set -- $(DEPS_$*)
	rm -f "$(ARTIFACTS)/src/poetry-$$current-python$${1##*:}.tar.gz"


.PHONY: all
all: sources dist build test
