BASE_DIR      	?= $(abspath .)
ARTIFACTS     	?= $(BASE_DIR)/artifacts
BUILD_CONTEXT 	?= $(BASE_DIR)
TEST_DIR		?= $(BASE_DIR)/test


.DEFAULT_GOAL := help

# список версий, которые поддерживает сборщик
# версии собираются одна за другой, строго по цепочке
VERSION_CHAIN		?= 4.21.3
# целевая версия сборки. по умолчанию берется последняя версия
# соответственно для ее сборки потребуется собрать все предыдущие версии в цепочки
VERSION      		?= $(lastword $(VERSION_CHAIN))
# выполнять ли сброс кэша: docker build --no-cache
NO_CACHE			?= 0

GLIBC_IMAGE			?= dmp/glibc:2.28
GCC_IMAGE			?= dmp/gcc:8.5.0
CMAKE_IMAGE			?= dmp/cmake:3.27.9-dist
PYTHON_IMAGE		?= dmp/python:3.10-dist

# зависимости для сборки разных версии
# версия        	runtime_image   build_image     python_image    nettle  gnutls  gnutls_series	krb5    krb5_series
DEPS_4.21.3		:=  $(GLIBC_IMAGE)	$(GCC_IMAGE)	$(PYTHON_IMAGE)  3.9.1   3.8.4   3.8          	1.21.3  1.21

# базовое название для образа
IMAGE			:= dmp/samba-ad
FIRST_VERSION	:= $(firstword $(VERSION_CHAIN))
NO_CACHE_TEXT	:= $(if $(filter 1,$(NO_CACHE)),--no-cache,)

# python-модули, нужные samba (waf) на сборке
PIP_PACKAGES	:= markdown dnspython cryptography

# dev-зависимости samba/krb5/gnutls/nettle (gcc/make/binutils/python — из образов)
APT_DEPS		:= \
		gawk bison flex file xz-utils gettext pkg-config 		\
		perl libparse-yapp-perl linux-libc-dev 					\
		zlib1g-dev libncurses5-dev libgdbm-dev libnss3-dev 		\
		libssl-dev libreadline-dev libffi-dev libsqlite3-dev 	\
		libbz2-dev liblzma-dev 									\
		libgmp-dev libtasn1-6-dev libidn2-dev libp11-kit-dev 	\
		libldap2-dev libjansson-dev liblmdb-dev libicu-dev 		\
		libacl1-dev libattr1-dev libblkid-dev libbsd-dev 		\
		libaio-dev libarchive-dev libgpgme-dev libpcap-dev 		\
		libpopt-dev libpam0g-dev libcap-dev libdbus-1-dev 		\
		libkeyutils-dev libverto-dev

# рантайм-зависимости AD DC
RUNTIME_APT_DEPS := \
		libldap-2.4-2 libjansson4 liblmdb0 libicu63 	\
		libacl1 libattr1 libblkid1 libbsd0 libaio1 		\
		libarchive13 libgpgme11 libpcap0.8 libpopt0 	\
		libcap2 libdbus-1-3 libkeyutils1 libverto1 		\
		libgmp10 libtasn1-6 libidn2-0 libssl1.1 zlib1g 	\
		dnsutils iproute2 procps


# helper: поиск предыдущей версии в VERSION_CHAIN
prev_of = $(shell echo "$(VERSION_CHAIN)" | awk -v v='$(1)' '{for(i=1;i<=NF;i++){if($$i==v){print p;exit};p=$$i}}')


.PHONY: help
help:
	@echo "make:"
	@echo "  make all        собрать всё"
	@echo "  make sources    скачать исходники для цепочки вплоть до $(VERSION)"
	@echo "  make dist       собрать цепочку -dist образов одну за другой до $(VERSION)"
	@echo "  make build      собрать образ $(IMAGE)"
	@echo "  make test       проверить образ $(IMAGE): версии samba/krb5, samba-tool, provision-smoke"
	@echo ""
	@echo "  Цепочка: $(VERSION_CHAIN)"
	@echo "  Зависимости (runtime_image/build_image/python_image/nettle/gnutls/krb5) задаются пословно в DEPS_<версия>."
	@echo "  Другая версия: make all VERSION=4.21.3"
	@echo ""


.PHONY: debug
debug:
	$(info BASE_DIR       = $(BASE_DIR))
	$(info ARTIFACTS      = $(ARTIFACTS))
	$(info BUILD_CONTEXT  = $(BUILD_CONTEXT))
	$(info TEST_DIR       = $(TEST_DIR))
	$(info VERSION_CHAIN  = $(VERSION_CHAIN))
	$(info VERSION        = $(VERSION))
	$(info FIRST_VERSION  = $(FIRST_VERSION))
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
	for spec in \
		"$(ARTIFACTS)/src/nettle-$$4.tar.gz=https://ftp.gnu.org/gnu/nettle/nettle-$$4.tar.gz" \
		"$(ARTIFACTS)/src/gnutls-$$5.tar.xz=https://www.gnupg.org/ftp/gcrypt/gnutls/v$$6/gnutls-$$5.tar.xz" \
		"$(ARTIFACTS)/src/krb5-$$7.tar.gz=https://kerberos.org/dist/krb5/$$8/krb5-$$7.tar.gz" \
		"$(ARTIFACTS)/src/samba-$$current.tar.gz=https://download.samba.org/pub/samba/stable/samba-$$current.tar.gz" \
	; do \
		dest="$${spec%%=*}"; url="$${spec#*=}"; \
		curl -v --skip-existing -L -o "$$dest" "$$url"; \
	done


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
	docker build -f Dockerfile $(NO_CACHE_TEXT) --target dist \
		--build-arg RUNTIME_IMAGE=$$1 				\
		--build-arg BUILD_IMAGE=$$2 				\
		--build-arg PYTHON_IMAGE=$$3 				\
		--build-arg NETTLE_VERSION=$$4 				\
		--build-arg GNUTLS_VERSION=$$5 				\
		--build-arg KRB5_VERSION=$$7 				\
		--build-arg SAMBA_VERSION=$$current 		\
		--build-arg PIP_PACKAGES="$(PIP_PACKAGES)" 	\
		--build-arg APT_DEPS="$(APT_DEPS)" 			\
		--tag $(IMAGE):$$current-dist 				\
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
	docker build -f Dockerfile $(NO_CACHE_TEXT) \
		--build-arg RUNTIME_IMAGE=$$1			\
		--build-arg BUILD_IMAGE=$$2				\
		--build-arg PYTHON_IMAGE=$$3			\
		--build-arg NETTLE_VERSION=$$4			\
		--build-arg GNUTLS_VERSION=$$5			\
		--build-arg KRB5_VERSION=$$7			\
		--build-arg SAMBA_VERSION=$$current		\
		--build-arg PIP_PACKAGES="$(PIP_PACKAGES)"			\
		--build-arg APT_DEPS="$(APT_DEPS)"		\
		--build-arg RUNTIME_APT_DEPS="$(RUNTIME_APT_DEPS)"	\
		--tag $(IMAGE):$$current				\
		--progress plain $(BUILD_CONTEXT)


# тестирование собранного образа
.PHONY: test
test: test-$(VERSION)

.ONESHELL:
test-%: build-%
	@current=$(*)
	docker run --rm --entrypoint /bin/bash -v $(TEST_DIR):/test:ro $(IMAGE):$$current -euc '\
		/test/adcheck.sh'


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
	rm -f	"$(ARTIFACTS)/src/nettle-$$4.tar.gz"	\
			"$(ARTIFACTS)/src/gnutls-$$5.tar.xz"	\
			"$(ARTIFACTS)/src/krb5-$$7.tar.gz"		\
			"$(ARTIFACTS)/src/samba-$$current.tar.gz"


.PHONY: all
all: sources dist build test
