SRC_DIR       ?= .
ARTIFACTS     ?= artifacts
BUILD_CONTEXT ?= .

.DEFAULT_GOAL := help
.PHONY: help sources gcc-dist build test all

ASTRA_VERSION  ?= 2.12
GLIBC_VERSION  ?= 2.28
GCC_VERSION      ?= 8.5.0
GMP_VERSION      ?= 6.1.2
MPFR_VERSION     ?= 4.0.2
MPC_VERSION      ?= 1.1.0
BINUTILS_VERSION ?= 2.31.1

IMAGE ?= astra-linux:$(ASTRA_VERSION)-glibc$(GLIBC_VERSION)-gcc$(GCC_VERSION)


help:
	@echo "make:"
	@echo "  make all        скачать всё для сборки и собрать образ astra-linux:$(ASTRA_VERSION)-glibc$(GLIBC_VERSION)-gcc$(GCC_VERSION)"
	@echo "  make sources    скачать всё для сборки: тарболы binutils/gcc/gmp/mpfr/mpc"
	@echo "  make gcc-dist   собрать промежуточный образ astra-linux:$(ASTRA_VERSION)-glibc$(GLIBC_VERSION)-gcc$(GCC_VERSION)-dist"
	@echo "  make build      собрать образ astra-linux:$(ASTRA_VERSION)-glibc$(GLIBC_VERSION)-gcc$(GCC_VERSION)"
	@echo "  make test       проверить тулчейн: собрать и запустить test/hello.cpp в образе $(IMAGE)"
	@echo ""


sources:
	@mkdir -p $(ARTIFACTS)/gcc-src
	@set -e; for spec in \
	  "$(ARTIFACTS)/gcc-src/gcc-$(GCC_VERSION).tar.xz=https://ftp.gnu.org/gnu/gcc/gcc-$(GCC_VERSION)/gcc-$(GCC_VERSION).tar.xz" \
	  "$(ARTIFACTS)/gcc-src/gmp-$(GMP_VERSION).tar.xz=https://ftp.gnu.org/gnu/gmp/gmp-$(GMP_VERSION).tar.xz" \
	  "$(ARTIFACTS)/gcc-src/mpfr-$(MPFR_VERSION).tar.xz=https://ftp.gnu.org/gnu/mpfr/mpfr-$(MPFR_VERSION).tar.xz" \
	  "$(ARTIFACTS)/gcc-src/mpc-$(MPC_VERSION).tar.gz=https://ftp.gnu.org/gnu/mpc/mpc-$(MPC_VERSION).tar.gz" \
	  "$(ARTIFACTS)/gcc-src/binutils-$(BINUTILS_VERSION).tar.xz=https://ftp.gnu.org/gnu/binutils/binutils-$(BINUTILS_VERSION).tar.xz" \
	; do \
	  dest="$${spec%%=*}"; url="$${spec#*=}"; \
	  if [ -f "$$dest" ]; then echo "  ✓ $$dest"; \
	  else echo ">>> $$url"; curl -fSL -o "$$dest" "$$url"; fi; \
	done

gcc-dist:
	docker build -f Dockerfile --target gcc-dist \
		--build-arg ASTRA_VERSION=$(ASTRA_VERSION) \
		--build-arg GLIBC_VERSION=$(GLIBC_VERSION) \
		--build-arg GCC_VERSION=$(GCC_VERSION) \
		--build-arg GMP_VERSION=$(GMP_VERSION) \
		--build-arg MPFR_VERSION=$(MPFR_VERSION) \
		--build-arg MPC_VERSION=$(MPC_VERSION) \
		--build-arg BINUTILS_VERSION=$(BINUTILS_VERSION) \
		--tag astra-linux:$(ASTRA_VERSION)-glibc$(GLIBC_VERSION)-gcc$(GCC_VERSION)-dist \
		--progress plain $(BUILD_CONTEXT)

build:
	docker build -f Dockerfile \
		--build-arg ASTRA_VERSION=$(ASTRA_VERSION) \
		--build-arg GLIBC_VERSION=$(GLIBC_VERSION) \
		--build-arg GCC_VERSION=$(GCC_VERSION) \
		--build-arg GMP_VERSION=$(GMP_VERSION) \
		--build-arg MPFR_VERSION=$(MPFR_VERSION) \
		--build-arg MPC_VERSION=$(MPC_VERSION) \
		--build-arg BINUTILS_VERSION=$(BINUTILS_VERSION) \
		--tag astra-linux:$(ASTRA_VERSION)-glibc$(GLIBC_VERSION)-gcc$(GCC_VERSION) \
		--progress plain $(BUILD_CONTEXT)

test:
	docker run --rm -v $(CURDIR)/test:/test:ro $(IMAGE) bash -euc '\
		export PATH=/usr/local/bin:$$PATH; \
		echo "== versions =="; \
		gcc  --version | head -1; \
		g++  --version | head -1; \
		as   --version | head -1; \
		ld   --version | head -1; \
		echo "== compile =="; \
		g++ -O2 -Wall /test/hello.cpp -o /tmp/hello; \
		echo "== run =="; \
		/tmp/hello'

all: sources gcc-dist build test
