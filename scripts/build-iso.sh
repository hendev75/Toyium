#!/usr/bin/env bash
# Toyium OS — build-iso.sh
# Creates hybrid BIOS+UEFI bootable ISO via grub-mkrescue
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="${ROOT}/build"
VERSION="$(cat "${ROOT}/VERSION" 2>/dev/null || echo "0.1.0")"
ARCH="x86_64"
ISO="${BUILD_DIR}/toyium-${VERSION}-${ARCH}.iso"
ISO_DIR="${BUILD_DIR}/iso"
KERNEL="${BUILD_DIR}/bzImage"
INITRAMFS="${BUILD_DIR}/initramfs.cpio.gz"
GRUB_CFG_SRC="${ROOT}/grub/grub.cfg"

if [[ ! -f "${KERNEL}" ]]; then
    echo "[iso] ERROR: kernel not found at ${KERNEL}. Run build-kernel.sh first." >&2
    exit 1
fi
if [[ ! -f "${INITRAMFS}" ]]; then
    echo "[iso] ERROR: initramfs not found at ${INITRAMFS}. Run build-initramfs.sh first." >&2
    exit 1
fi

if ! command -v grub-mkrescue >/dev/null 2>&1 && ! command -v grub2-mkrescue >/dev/null 2>&1; then
    echo "[iso] ERROR: grub-mkrescue not found. Install grub-pc-bin + xorriso + mtools" >&2
    echo "       sudo apt install grub-pc-bin grub-efi-amd64-bin xorriso mtools" >&2
    exit 1
fi
if ! command -v xorriso >/dev/null 2>&1; then
    echo "[iso] ERROR: xorriso not found. sudo apt install xorriso" >&2
    exit 1
fi

# pick grub-mkrescue binary
GRUB_MKRESCUE="grub-mkrescue"
if ! command -v grub-mkrescue >/dev/null; then
    GRUB_MKRESCUE="grub2-mkrescue"
fi

echo "[iso] assembling ISO tree -> ${ISO_DIR}"
rm -rf "${ISO_DIR}"
mkdir -p "${ISO_DIR}/boot/grub"

cp -v "${KERNEL}" "${ISO_DIR}/boot/bzImage"
cp -v "${INITRAMFS}" "${ISO_DIR}/boot/initramfs.cpio.gz"

if [[ -f "${GRUB_CFG_SRC}" ]]; then
    cp -v "${GRUB_CFG_SRC}" "${ISO_DIR}/boot/grub/grub.cfg"
else
    echo "[iso] WARNING: grub/grub.cfg not found, generating default"
    cat > "${ISO_DIR}/boot/grub/grub.cfg" <<'GRUB'
set timeout=5
set default=0
menuentry "Toyium OS" {
    linux /boot/bzImage console=tty0 console=ttyS0,115200n8
    initrd /boot/initramfs.cpio.gz
}
GRUB
fi

# version file on ISO
echo "${VERSION} ${ARCH} $(date -u +%Y-%m-%dT%H:%M:%SZ)" > "${ISO_DIR}/boot/toyium-version"

echo "[iso] running ${GRUB_MKRESCUE} ..."
# grub-mkrescue wraps xorriso; needs mtools
"${GRUB_MKRESCUE}" -o "${ISO}" "${ISO_DIR}" 2>&1 | tail -n 20

if [[ ! -f "${ISO}" ]]; then
    echo "[iso] ERROR: ISO not created" >&2
    exit 1
fi

echo "[iso] -> ${ISO} ($(du -h "${ISO}" | cut -f1)) ✓"
echo "[iso] hybrid ISO ready. Test with: make run-iso  or  ./scripts/run-qemu.sh --iso"
# verify hybrid
if command -v fdisk >/dev/null; then
    echo "[iso] partition table:"
    fdisk -l "${ISO}" 2>/dev/null | head -n 20 || true
fi
echo "[iso] done."
