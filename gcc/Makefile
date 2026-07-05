SRC_DIR       ?= .
ARTIFACTS     ?= artifacts
BUILD_CONTEXT ?= .

.DEFAULT_GOAL := help
.PHONY: help sources dist build test all

ASTRA_VERSION  ?= 2.12
GLIBC_VERSION  ?= 2.28
GCC_VERSION      ?= 8.5.0
GMP_VERSION      ?= 6.1.2
MPFR_VERSION     ?= 4.0.2
MPC_VERSION      ?= 1.1.0
BINUTILS_VERSION ?= 2.31.1
MAKE_VERSION     ?= 4.4.1

BASE_IMAGE=dmp/glibc:$(GLIBC_VERSION)

IMAGE ?= dmp/gcc:$(GCC_VERSION)


help:
	@echo "make:"
	@echo "  make all        собрать всё"
	@echo "  make sources    скачать всё для сборки"
	@echo "  make dist   	 собрать промежуточный образ-дистрибутив $(IMAGE)-dist"
	@echo "  make build      собрать образ $(IMAGE)"
	@echo "  make test       проверить тулчейн: собрать и запустить test/hello.cpp в образе $(IMAGE)"
	@echo ""


debug:
	@echo BASE_IMAGE: $(BASE_IMAGE)
	@echo IMAGE: $(IMAGE)

sources:
	@mkdir -p $(ARTIFACTS)/src
	@set -e; for spec in \
	  "$(ARTIFACTS)/src/gcc-$(GCC_VERSION).tar.xz=https://ftp.gnu.org/gnu/gcc/gcc-$(GCC_VERSION)/gcc-$(GCC_VERSION).tar.xz" \
	  "$(ARTIFACTS)/src/gmp-$(GMP_VERSION).tar.xz=https://ftp.gnu.org/gnu/gmp/gmp-$(GMP_VERSION).tar.xz" \
	  "$(ARTIFACTS)/src/mpfr-$(MPFR_VERSION).tar.xz=https://ftp.gnu.org/gnu/mpfr/mpfr-$(MPFR_VERSION).tar.xz" \
	  "$(ARTIFACTS)/src/mpc-$(MPC_VERSION).tar.gz=https://ftp.gnu.org/gnu/mpc/mpc-$(MPC_VERSION).tar.gz" \
	  "$(ARTIFACTS)/src/binutils-$(BINUTILS_VERSION).tar.xz=https://ftp.gnu.org/gnu/binutils/binutils-$(BINUTILS_VERSION).tar.xz" \
	  "$(ARTIFACTS)/src/make-$(MAKE_VERSION).tar.gz=https://ftp.gnu.org/gnu/make/make-$(MAKE_VERSION).tar.gz" \
	; do \
	  dest="$${spec%%=*}"; url="$${spec#*=}"; \
	  if [ -f "$$dest" ]; then echo "  ✓ $$dest"; \
	  else echo ">>> $$url"; curl -fSL -o "$$dest" "$$url"; fi; \
	done


dist:
	docker build -f Dockerfile --target dist \
		--build-arg BASE_IMAGE=$(BASE_IMAGE) \
		--build-arg GCC_VERSION=$(GCC_VERSION) \
		--build-arg GMP_VERSION=$(GMP_VERSION) \
		--build-arg MPFR_VERSION=$(MPFR_VERSION) \
		--build-arg MPC_VERSION=$(MPC_VERSION) \
		--build-arg BINUTILS_VERSION=$(BINUTILS_VERSION) \
		--build-arg MAKE_VERSION=$(MAKE_VERSION) \
		--tag $(IMAGE)-dist \
		--progress plain $(BUILD_CONTEXT)


build:
	docker build -f Dockerfile \
		--build-arg BASE_IMAGE=$(BASE_IMAGE) \
		--build-arg GCC_VERSION=$(GCC_VERSION) \
		--build-arg GMP_VERSION=$(GMP_VERSION) \
		--build-arg MPFR_VERSION=$(MPFR_VERSION) \
		--build-arg MPC_VERSION=$(MPC_VERSION) \
		--build-arg BINUTILS_VERSION=$(BINUTILS_VERSION) \
		--build-arg MAKE_VERSION=$(MAKE_VERSION) \
		--tag $(IMAGE) \
		--progress plain $(BUILD_CONTEXT)


test:
	docker run --rm -v $(CURDIR)/test:/test:ro $(IMAGE) bash -euc '\
		export PATH=/usr/local/bin:$$PATH; \
		echo "== versions =="; \
		gcc  --version | head -1; \
		g++  --version | head -1; \
		as   --version | head -1; \
		ld   --version | head -1; \
		make --version | head -1; \
		echo "== compile =="; \
		g++ -O2 -Wall /test/hello.cpp -o /tmp/hello; \
		echo "== run =="; \
		/tmp/hello'


all: sources dist build test
