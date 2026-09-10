#!/usr/bin/env bash
# Toyium OS — build-busybox.sh
# Downloads + builds BusyBox 1.36.1 static
set -euo pipefail

VERSION="1.36.1"
URL="https://busybox.net/downloads/busybox-${VERSION}.tar.bz2"
BUILD_DIR="$(cd "$(dirname "$0")/.." && pwd)/build"
SRC_DIR="${BUILD_DIR}/busybox-${VERSION}"
CONFIG_SRC="$(cd "$(dirname "$0")/.." && pwd)/configs/busybox.config"
JOBS="$(nproc 2>/dev/null || echo 4)"

mkdir -p "${BUILD_DIR}"

if [[ ! -d "${SRC_DIR}" ]]; then
    echo "[busybox] downloading busybox-${VERSION} ..."
    TMP_TAR="${BUILD_DIR}/busybox-${VERSION}.tar.bz2"
    if [[ ! -f "${TMP_TAR}" ]]; then
        if command -v wget >/dev/null; then
            wget -O "${TMP_TAR}" "${URL}"
        else
            curl -L -o "${TMP_TAR}" "${URL}"
        fi
    fi
    echo "[busybox] extracting ..."
    tar -xf "${TMP_TAR}" -C "${BUILD_DIR}"
else
    echo "[busybox] source already present: ${SRC_DIR}"
fi

if [[ -f "${CONFIG_SRC}" ]]; then
    echo "[busybox] using Toyium busybox config"
    cp "${CONFIG_SRC}" "${SRC_DIR}/.config"
else
    echo "[busybox] generating default config"
    make -C "${SRC_DIR}" defconfig
    # enable static
    sed -i 's/# CONFIG_STATIC is not set/CONFIG_STATIC=y/' "${SRC_DIR}/.config" || true
fi

echo "[busybox] building with -j${JOBS} ..."
make -C "${SRC_DIR}" -j"${JOBS}"
make -C "${SRC_DIR}" install CONFIG_PREFIX="${BUILD_DIR}/busybox-root"

# Verify
if [[ ! -f "${BUILD_DIR}/busybox-root/bin/busybox" ]]; then
    echo "[busybox] ERROR: busybox binary not found" >&2
    exit 1
fi

echo "[busybox] -> ${BUILD_DIR}/busybox-root ($(du -sh "${BUILD_DIR}/busybox-root" | cut -f1))"
# check static
if file "${BUILD_DIR}/busybox-root/bin/busybox" | grep -q "statically linked"; then
    echo "[busybox] binary is statically linked ✓"
else
    echo "[busybox] WARNING: binary is dynamically linked — may need musl/glibc in initramfs"
    file "${BUILD_DIR}/busybox-root/bin/busybox"
fi
echo "[busybox] done."
