BASE_DIR      	?= .
ARTIFACTS     	?= $(BASE_DIR)/artifacts
BUILD_CONTEXT 	?= $(BASE_DIR)
TEST_DIR		?= $(BASE_DIR)/test


.DEFAULT_GOAL := help
.PHONY: help sources dist build test all

GLIBC_VERSION	?= 2.28
GCC_VERSION		?= 8.5.0
PYTHON_VERSION	?= 3.11
SAMBA_VERSION	?= 4.21.3
NETTLE_VERSION	?= 3.9.1
GNUTLS_VERSION	?= 3.8.4
GNUTLS_SERIES	?= 3.8
KRB5_VERSION	?= 1.21.3
KRB5_SERIES		?= 1.21
NO_CACHE		?= 0

BASE_IMAGE		:= dmp/gcc:$(GCC_VERSION)
PYTHON_IMAGE	:= dmp/python:$(PYTHON_VERSION)-dist
RUNTIME_IMAGE	:= dmp/glibc:$(GLIBC_VERSION)
IMAGE			:= dmp/samba-ad:$(SAMBA_VERSION)
NO_CACHE_TEXT	:= $(if $(filter 1,$(NO_CACHE)),--no-cache,)

# python-модули, нужные samba (waf) на сборке
PIP_PACKAGES	:= markdown dnspython cryptography

# dev-зависимости samba/krb5/gnutls/nettle (gcc/make/binutils/python — из образов)
APT_DEPS		:= \
		gawk bison flex file xz-utils gettext pkg-config \
		perl libparse-yapp-perl linux-libc-dev \
		zlib1g-dev libncurses5-dev libgdbm-dev libnss3-dev \
		libssl-dev libreadline-dev libffi-dev libsqlite3-dev \
		libbz2-dev liblzma-dev \
		libgmp-dev libtasn1-6-dev libidn2-dev libp11-kit-dev \
		libldap2-dev libjansson-dev liblmdb-dev libicu-dev \
		libacl1-dev libattr1-dev libblkid-dev libbsd-dev \
		libaio-dev libarchive-dev libgpgme-dev libpcap-dev \
		libpopt-dev libpam0g-dev libcap-dev libdbus-1-dev \
		libkeyutils-dev libverto-dev

# рантайм-зависимости AD DC
RUNTIME_APT_DEPS := \
		libldap-2.4-2 libjansson4 liblmdb0 libicu63 \
		libacl1 libattr1 libblkid1 libbsd0 libaio1 \
		libarchive13 libgpgme11 libpcap0.8 libpopt0 \
		libcap2 libdbus-1-3 libkeyutils1 libverto1 \
		libgmp10 libtasn1-6 libidn2-0 libssl1.1 zlib1g \
		dnsutils iproute2 procps


help:
	@echo "make:"
	@echo "  make all        собрать всё"
	@echo "  make sources    скачать исходники nettle/gnutls/krb5/samba"
	@echo "  make dist       собрать промежуточный образ-дистрибутив $(IMAGE)-dist"
	@echo "  make build      собрать образ $(IMAGE) (сборщик: $(BASE_IMAGE), python: $(PYTHON_IMAGE))"
	@echo "  make test       проверить образ $(IMAGE): версии samba/krb5, samba-tool, provision-smoke"
	@echo ""


debug:
	@echo BASE_IMAGE: $(BASE_IMAGE)
	@echo PYTHON_IMAGE: $(PYTHON_IMAGE)
	@echo RUNTIME_IMAGE: $(RUNTIME_IMAGE)
	@echo IMAGE: $(IMAGE)
	@echo NO_CACHE_TEXT: $(NO_CACHE_TEXT)


sources:
	@mkdir -p $(ARTIFACTS)/src
	@set -e; for spec in \
	  "$(ARTIFACTS)/src/nettle-$(NETTLE_VERSION).tar.gz=https://ftp.gnu.org/gnu/nettle/nettle-$(NETTLE_VERSION).tar.gz" \
	  "$(ARTIFACTS)/src/gnutls-$(GNUTLS_VERSION).tar.xz=https://www.gnupg.org/ftp/gcrypt/gnutls/v$(GNUTLS_SERIES)/gnutls-$(GNUTLS_VERSION).tar.xz" \
	  "$(ARTIFACTS)/src/krb5-$(KRB5_VERSION).tar.gz=https://kerberos.org/dist/krb5/$(KRB5_SERIES)/krb5-$(KRB5_VERSION).tar.gz" \
	  "$(ARTIFACTS)/src/samba-$(SAMBA_VERSION).tar.gz=https://download.samba.org/pub/samba/stable/samba-$(SAMBA_VERSION).tar.gz" \
	; do \
	  dest="$${spec%%=*}"; url="$${spec#*=}"; \
	  if [ -f "$$dest" ]; then echo "  ✓ $$dest"; \
	  else echo ">>> $$url"; curl -fSL -o "$$dest" "$$url"; fi; \
	done


dist:
	docker build -f Dockerfile --target dist $(NO_CACHE_TEXT) \
		--build-arg BASE_IMAGE=$(BASE_IMAGE) \
		--build-arg PYTHON_IMAGE=$(PYTHON_IMAGE) \
		--build-arg RUNTIME_IMAGE=$(RUNTIME_IMAGE) \
		--build-arg NETTLE_VERSION=$(NETTLE_VERSION) \
		--build-arg GNUTLS_VERSION=$(GNUTLS_VERSION) \
		--build-arg KRB5_VERSION=$(KRB5_VERSION) \
		--build-arg SAMBA_VERSION=$(SAMBA_VERSION) \
		--build-arg PIP_PACKAGES="$(PIP_PACKAGES)" \
		--build-arg APT_DEPS="$(APT_DEPS)" \
		--tag $(IMAGE)-dist \
		--progress plain $(BUILD_CONTEXT)


build:
	docker build -f Dockerfile $(NO_CACHE_TEXT) \
		--build-arg BASE_IMAGE=$(BASE_IMAGE) \
		--build-arg PYTHON_IMAGE=$(PYTHON_IMAGE) \
		--build-arg RUNTIME_IMAGE=$(RUNTIME_IMAGE) \
		--build-arg NETTLE_VERSION=$(NETTLE_VERSION) \
		--build-arg GNUTLS_VERSION=$(GNUTLS_VERSION) \
		--build-arg KRB5_VERSION=$(KRB5_VERSION) \
		--build-arg SAMBA_VERSION=$(SAMBA_VERSION) \
		--build-arg PIP_PACKAGES="$(PIP_PACKAGES)" \
		--build-arg APT_DEPS="$(APT_DEPS)" \
		--build-arg RUNTIME_APT_DEPS="$(RUNTIME_APT_DEPS)" \
		--tag $(IMAGE) \
		--progress plain $(BUILD_CONTEXT)


test:
	docker run --rm --entrypoint /bin/bash -v $(TEST_DIR):/test:ro $(IMAGE) -euc '\
		/test/adcheck.sh'


all: sources dist build test
