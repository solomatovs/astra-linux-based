BASE_DIR      	?= $(abspath .)
ARTIFACTS     	?= $(abspath $(BASE_DIR)/../../ci-artifacts/nginx)
BUILD_CONTEXT 	?= $(BASE_DIR)
# рабочая копия $(ARTIFACTS) внутри контекста сборки (хардлинки, см. artifacts-link)
LINKED_ARTIFACTS ?= $(BUILD_CONTEXT)/artifacts
TEST_DIR		?= $(BASE_DIR)/test


.DEFAULT_GOAL := help

# список версий, которые поддерживает сборщик
# версии nginx независимы, но собираются одна за другой, строго по цепочке
VERSION_CHAIN		?= 1.24.0 1.26.3 1.27.4 1.28.0
# целевая версия сборки. по умолчанию берется последняя версия
VERSION      		?= $(lastword $(VERSION_CHAIN))
# базовые url адреса (конкретные версии подставляются в цели sources)
# nginx
BASE_URL			?= https://nginx.org/download
# pcre2
PCRE2_URL			?= https://github.com/PCRE2Project/pcre2/releases/download
# zlib
ZLIB_URL			?= https://github.com/madler/zlib/releases/download
# openssl
OPENSSL_URL			?= https://github.com/openssl/openssl/releases/download
# docker build --no-cache
NO_CACHE			?= 0
# apt-deps (build)
APT_DEPS			?= perl bison flex file

GLIBC_IMAGE			?= dmp/glibc:2.28
GCC_IMAGE			?= dmp/gcc:8.5.0-dist

# зависимости для сборки разных версии
# версия           runtime_image	build_image		pcre2	zlib	openssl		http3
DEPS_1.24.0		:= $(GLIBC_IMAGE)	$(GCC_IMAGE)	10.45	1.3.1	3.0.16		0
DEPS_1.26.3		:= $(GLIBC_IMAGE)	$(GCC_IMAGE)	10.45	1.3.1	3.0.16		1
DEPS_1.27.4		:= $(GLIBC_IMAGE)	$(GCC_IMAGE)	10.45	1.3.1	3.0.16		1
DEPS_1.28.0		:= $(GLIBC_IMAGE)	$(GCC_IMAGE)	10.45	1.3.1	3.0.16		1

# базовое название для образа
IMAGE			:= dmp/nginx
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
	@echo "  make sources    скачать исходники (nginx/pcre2/zlib/openssl) для $(VERSION)"
	@echo "  make dist       собрать -dist образ $(VERSION) (звенья цепочки — если нужны)"
	@echo "  make build      собрать образ $(IMAGE)"
	@echo "  make test       проверить образ: nginx -V, -t, модули, запуск и HTTP-ответ"
	@echo ""
	@echo "  Цепочка: $(VERSION_CHAIN)"
	@echo "  Каждый образ тегируется полной и короткой версией (напр. 1.28.0 и 1.28)."
	@echo "  Версии зависимостей и флаг http3 — в DEPS_<версия> (http_v3 только nginx >= 1.25)."
	@echo "  Свой конфиг: nginx.conf.example -> nginx.conf."
	@echo "  Другая версия: make all VERSION=1.26.3"
	@echo ""
	@echo "  Сторонние модули (spnego, auth-ldap, njs) собираются отдельно: см. ../nginx-ext."
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
	$(info PCRE2_URL      = $(PCRE2_URL))
	$(info ZLIB_URL       = $(ZLIB_URL))
	$(info OPENSSL_URL    = $(OPENSSL_URL))
	$(info NO_CACHE       = $(NO_CACHE))
	$(info NO_CACHE_TEXT  = $(NO_CACHE_TEXT))
	$(info APT_DEPS       = $(APT_DEPS))
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
	pcre2=$$3; zlib=$$4; openssl=$$5
	mkdir -p $(ARTIFACTS)/src
	# полные url собираются из базового url и версии (версии — в DEPS_<версия>)
	[ -f "$(ARTIFACTS)/src/nginx-$$current.tar.gz" ] || curl -v -L -o "$(ARTIFACTS)/src/nginx-$$current.tar.gz" "$(BASE_URL)/nginx-$$current.tar.gz"
	[ -f "$(ARTIFACTS)/src/pcre2-$$pcre2.tar.gz" ] || curl -v -L -o "$(ARTIFACTS)/src/pcre2-$$pcre2.tar.gz" "$(PCRE2_URL)/pcre2-$$pcre2/pcre2-$$pcre2.tar.gz"
	[ -f "$(ARTIFACTS)/src/zlib-$$zlib.tar.gz" ] || curl -v -L -o "$(ARTIFACTS)/src/zlib-$$zlib.tar.gz" "$(ZLIB_URL)/v$$zlib/zlib-$$zlib.tar.gz"
	[ -f "$(ARTIFACTS)/src/openssl-$$openssl.tar.gz" ] || curl -v -L -o "$(ARTIFACTS)/src/openssl-$$openssl.tar.gz" "$(OPENSSL_URL)/openssl-$$openssl/openssl-$$openssl.tar.gz"


# docker build --target dist - дистрибутив со скомпилированными исходниками (без установки в систему)
.PHONY: dist
dist: dist-$(VERSION)

.ONESHELL:
dist-%: $$(call need_dist,$$*)
	@current=$(*)
	set -e
	set -- $(DEPS_$*)
	runtime=$$1; build=$$2; pcre2=$$3; zlib=$$4; openssl=$$5; http3=$$6
	short=$${current%.*}
	[ -z "$(call built,$(IMAGE):$*-dist)" ] || { echo "  = $(IMAGE):$*-dist уже собран (FORCE=1 — пересобрать)"; exit 0; }
	docker build -f Dockerfile $(NO_CACHE_TEXT) --target dist \
		--build-arg RUNTIME_IMAGE=$$runtime 	\
		--build-arg BUILD_IMAGE=$$build 		\
		--build-arg NGINX_VERSION=$$current 	\
		--build-arg PCRE2_VERSION=$$pcre2 		\
		--build-arg ZLIB_VERSION=$$zlib 		\
		--build-arg OPENSSL_VERSION=$$openssl 	\
		--build-arg NGINX_HTTP3=$$http3 		\
		--build-arg APT_DEPS="$(APT_DEPS)" 		\
		--tag $(IMAGE):$$current-dist 			\
		--tag $(IMAGE):$$short-dist 			\
		--progress plain $(BUILD_CONTEXT)


# docker build - итоговый образ с установленным ПО (установка из дистрибутива)
.PHONY: build
build: build-$(VERSION)

.ONESHELL:
build-%: $$(call need_build,$$*)
	@current=$(*)
	set -e
	set -- $(DEPS_$*)
	runtime=$$1; build=$$2; pcre2=$$3; zlib=$$4; openssl=$$5; http3=$$6
	short=$${current%.*}
	[ -z "$(call built,$(IMAGE):$*)" ] || { echo "  = $(IMAGE):$* уже собран (FORCE=1 — пересобрать)"; exit 0; }
	docker build -f Dockerfile $(NO_CACHE_TEXT) 	\
		--build-arg DIST_IMAGE=$(IMAGE):$*-dist	\
		--build-arg RUNTIME_IMAGE=$$runtime 		\
		--build-arg BUILD_IMAGE=$$build 			\
		--build-arg NGINX_VERSION=$$current 		\
		--build-arg PCRE2_VERSION=$$pcre2 			\
		--build-arg ZLIB_VERSION=$$zlib 			\
		--build-arg OPENSSL_VERSION=$$openssl 		\
		--build-arg NGINX_HTTP3=$$http3 			\
		--build-arg APT_DEPS="$(APT_DEPS)" 			\
		--tag $(IMAGE):$$current 					\
		--tag $(IMAGE):$$short 						\
		--progress plain $(BUILD_CONTEXT)


# тестирование собранного образа
.PHONY: test
test: test-$(VERSION)

.ONESHELL:
test-%: build-%
	@current=$(*)
	docker run --rm --entrypoint /bin/bash -v $(TEST_DIR):/test:ro $(IMAGE):$$current -euc '\
		echo "== version =="; 					\
		nginx -v; 								\
		echo "== configure (-V) =="; 			\
		nginx -V; 								\
		echo "== config test (-t) =="; 			\
		nginx -t; 								\
		echo "== functional check =="; 			\
		/test/nginxcheck.sh'


.PHONY: clean
clean: clean-$(VERSION)
	rm -rf "$(LINKED_ARTIFACTS)"

.ONESHELL:
clean-%:
	@current=$(*)
	set -e
	set -- $(DEPS_$*)
	pcre2=$$3; zlib=$$4; openssl=$$5
	rm -f "$(ARTIFACTS)/src/nginx-$$current.tar.gz" \
		"$(ARTIFACTS)/src/pcre2-$$pcre2.tar.gz" \
		"$(ARTIFACTS)/src/zlib-$$zlib.tar.gz" \
		"$(ARTIFACTS)/src/openssl-$$openssl.tar.gz"


.PHONY: all
all: sources dist build test
