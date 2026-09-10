#!/usr/bin/env bash
# Toyium OS — build-kernel.sh
# Downloads + builds Linux 6.6 LTS (x86_64, minimal, bzImage)
set -euo pipefail

VERSION="6.6.58"
URL="https://cdn.kernel.org/pub/linux/kernel/v6.x/linux-${VERSION}.tar.xz"
BUILD_DIR="$(cd "$(dirname "$0")/.." && pwd)/build"
SRC_DIR="${BUILD_DIR}/linux-${VERSION}"
CONFIG_SRC="$(cd "$(dirname "$0")/.." && pwd)/configs/kernel.config"
BZIMAGE="${SRC_DIR}/arch/x86/boot/bzImage"
OUT="${BUILD_DIR}/bzImage"
JOBS="$(nproc 2>/dev/null || echo 4)"

mkdir -p "${BUILD_DIR}"

# --- download ---
if [[ ! -d "${SRC_DIR}" ]]; then
    echo "[kernel] downloading linux-${VERSION} ..."
    TMP_TAR="${BUILD_DIR}/linux-${VERSION}.tar.xz"
    if [[ ! -f "${TMP_TAR}" ]]; then
        if command -v wget >/dev/null; then
            wget -O "${TMP_TAR}" "${URL}"
        else
            curl -L -o "${TMP_TAR}" "${URL}"
        fi
    fi
    echo "[kernel] extracting ..."
    tar -xf "${TMP_TAR}" -C "${BUILD_DIR}"
else
    echo "[kernel] source already present: ${SRC_DIR}"
fi

# --- config ---
if [[ -f "${CONFIG_SRC}" ]]; then
    echo "[kernel] using Toyium kernel config: ${CONFIG_SRC}"
    cp "${CONFIG_SRC}" "${SRC_DIR}/.config"
    # ensure 64-bit
    make -C "${SRC_DIR}" olddefconfig
else
    echo "[kernel] no configs/kernel.config found — generating x86_64 defconfig"
    make -C "${SRC_DIR}" x86_64_defconfig
    # minimal tweaks for initramfs + virtio
    scripts/config -e BLK_DEV_INITRD -e INITRAMFS_SOURCE -e RD_GZIP -e TMPFS -e DEVTMPFS -e DEVTMPFS_MOUNT \
                   -e VIRTIO -e VIRTIO_BLK -e VIRTIO_NET -e VIRTIO_PCI -e NET -e INET -e E1000 -e SERIAL_8250 -e TTY 2>/dev/null || true
fi

# --- build ---
echo "[kernel] building bzImage with -j${JOBS} (this may take 10-30 min) ..."
make -C "${SRC_DIR}" -j"${JOBS}" bzImage

if [[ ! -f "${BZIMAGE}" ]]; then
    echo "[kernel] ERROR: bzImage not found at ${BZIMAGE}" >&2
    exit 1
fi

cp -v "${BZIMAGE}" "${OUT}"
echo "[kernel] -> ${OUT} ($(du -h "${OUT}" | cut -f1))"
echo "[kernel] done."
