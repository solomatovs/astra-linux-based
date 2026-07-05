SRC_DIR       ?= .
ARTIFACTS     ?= artifacts
BUILD_CONTEXT ?= .

.DEFAULT_GOAL := help
.PHONY: help sources build

ASTRA_VERSION  ?= 2.12
GLIBC_VERSION  ?= 2.28
GCC_VERSION    ?= 8.5.0
GMP_VERSION    ?= 6.1.2
MPFR_VERSION   ?= 4.0.2
MPC_VERSION    ?= 1.1.0


help:
	@echo "make:"
	@echo "  make all        скачать всё для сборки и собрать образ astra-linux:$(ASTRA_VERSION)-glibc$(GLIBC_VERSION)-gcc$(GCC_VERSION)"
	@echo "  make sources    скачать всё для сборки: тарболы gcc/gmp/mpfr/mpc"
	@echo "  make build      собрать образ astra-linux:$(ASTRA_VERSION)-glibc$(GLIBC_VERSION)-gcc$(GCC_VERSION)"
	@echo ""


sources:
	@mkdir -p $(ARTIFACTS)/gcc-src
	@set -e; for spec in \
	  "$(ARTIFACTS)/gcc-src/gcc-$(GCC_VERSION).tar.xz=https://ftp.gnu.org/gnu/gcc/gcc-$(GCC_VERSION)/gcc-$(GCC_VERSION).tar.xz" \
	  "$(ARTIFACTS)/gcc-src/gmp-$(GMP_VERSION).tar.xz=https://ftp.gnu.org/gnu/gmp/gmp-$(GMP_VERSION).tar.xz" \
	  "$(ARTIFACTS)/gcc-src/mpfr-$(MPFR_VERSION).tar.xz=https://ftp.gnu.org/gnu/mpfr/mpfr-$(MPFR_VERSION).tar.xz" \
	  "$(ARTIFACTS)/gcc-src/mpc-$(MPC_VERSION).tar.gz=https://ftp.gnu.org/gnu/mpc/mpc-$(MPC_VERSION).tar.gz" \
	; do \
	  dest="$${spec%%=*}"; url="$${spec#*=}"; \
	  if [ -f "$$dest" ]; then echo "  ✓ $$dest"; \
	  else echo ">>> $$url"; curl -fSL -o "$$dest" "$$url"; fi; \
	done

build:
	docker build -f Dockerfile \
		--build-arg ASTRA_VERSION=$(ASTRA_VERSION) \
		--build-arg GLIBC_VERSION=$(GLIBC_VERSION) \
		--build-arg GCC_VERSION=$(GCC_VERSION) \
		--build-arg GMP_VERSION=$(GMP_VERSION) \
		--build-arg MPFR_VERSION=$(MPFR_VERSION) \
		--build-arg MPC_VERSION=$(MPC_VERSION) \
		--tag astra-linux:$(ASTRA_VERSION)-glibc$(GLIBC_VERSION)-gcc$(GCC_VERSION) \
		--progress plain $(BUILD_CONTEXT)

all: sources build
