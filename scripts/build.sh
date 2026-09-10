#!/usr/bin/env bash
# Toyium OS — build.sh (full orchestrator)
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="${ROOT}/build"
VERSION="$(cat "${ROOT}/VERSION" 2>/dev/null || echo "0.1.0")"

echo ""
echo "  _____           _"
echo " |_   _|__  _   _(_)_   _ _ __ ___"
echo "   | |/ _ \| | | | | | | | '_ \` _ \\"
echo "   | | (_) | |_| | | |_| | | | | | |"
echo "   |_|\___/ \__, |_|\__,_|_| |_| |_|"
echo "            |___/  Toyium OS v${VERSION} — Full Build"
echo ""

mkdir -p "${BUILD_DIR}"

# check deps
echo "[build] checking dependencies ..."
MISSING=()
for cmd in gcc make cpio gzip tar bison flex bc; do
    if ! command -v "$cmd" >/dev/null 2>&1; then
        MISSING+=("$cmd")
    fi
done
if [[ ${#MISSING[@]} -gt 0 ]]; then
    echo "[build] WARNING: missing tools: ${MISSING[*]}"
    echo "        sudo apt install build-essential bc bison flex libelf-dev libssl-dev cpio"
fi

STEPS=()
# allow selective build via args: ./build.sh kernel busybox initramfs iso
if [[ $# -gt 0 ]]; then
    STEPS=("$@")
else
    STEPS=(kernel busybox initramfs iso)
fi

for step in "${STEPS[@]}"; do
    case "$step" in
        kernel)    bash "${ROOT}/scripts/build-kernel.sh" ;;
        busybox)   bash "${ROOT}/scripts/build-busybox.sh" ;;
        initramfs) bash "${ROOT}/scripts/build-initramfs.sh" ;;
        iso)       bash "${ROOT}/scripts/build-iso.sh" ;;
        *) echo "[build] unknown step: $step" >&2; exit 1 ;;
    esac
done

echo ""
echo "[build] Toyium OS v${VERSION} build complete ✓"
echo "        Kernel:    ${BUILD_DIR}/bzImage"
echo "        Initramfs: ${BUILD_DIR}/initramfs.cpio.gz"
echo "        ISO:       ${BUILD_DIR}/toyium-${VERSION}-x86_64.iso"
echo ""
echo "  Run:  make run        # QEMU direct"
echo "        make run-iso    # QEMU ISO (GRUB)"
echo ""
