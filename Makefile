###############################################################################
# Toyium OS — Top-level Makefile
# 64-bit Linux-based, non-graphical, command-line OS
###############################################################################

VERSION   := $(shell cat VERSION 2>/dev/null || echo "0.1.0")
ARCH      := x86_64
ISO       := build/toyium-$(VERSION)-$(ARCH).iso
KERNEL    := build/bzImage
INITRAMFS := build/initramfs.cpio.gz

JOBS      := $(shell nproc 2>/dev/null || echo 4)

.DEFAULT_GOAL := help

.PHONY: help all kernel busybox initramfs iso run run-iso run-nographic clean distclean

help: ## Show this help
	@echo ""
	@echo "  Toyium OS v$(VERSION) — $(ARCH) — Make targets"
	@echo "  =============================================="
	@grep -E '^[a-zA-Z_-]+:.*?## .*$$' $(MAKEFILE_LIST) | awk 'BEGIN {FS = ":.*?## "}; {printf "  \033[36m%-18s\033[0m %s\n", $$1, $$2}'
	@echo ""
	@echo "  Examples:"
	@echo "    make all              # full build -> $(ISO)"
	@echo "    make run              # QEMU direct boot"
	@echo "    make run-iso          # QEMU ISO boot"
	@echo "    make clean            # remove build artifacts"
	@echo ""

all: iso ## Full build (kernel -> busybox -> initramfs -> ISO)

kernel: ## Build Linux kernel -> build/bzImage
	@bash scripts/build-kernel.sh

busybox: ## Build BusyBox (static)
	@bash scripts/build-busybox.sh

initramfs: ## Build initramfs -> build/initramfs.cpio.gz
	@bash scripts/build-initramfs.sh

iso: kernel busybox initramfs ## Build bootable ISO -> build/toyium-*.iso
	@bash scripts/build-iso.sh

run: ## Boot in QEMU (direct kernel, fastest)
	@bash scripts/run-qemu.sh --direct

run-iso: ## Boot ISO in QEMU (tests GRUB)
	@bash scripts/run-qemu.sh --iso

run-nographic: ## Boot headless (serial console)
	@bash scripts/run-qemu.sh --direct --nographic

clean: ## Remove build outputs (keep sources)
	@bash scripts/clean.sh

distclean: ## Remove everything including downloaded sources
	@bash scripts/clean.sh --distclean

# quick aliases
qemu: run
iso-run: run-iso
