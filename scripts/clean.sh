#!/usr/bin/env bash
# Toyium OS — clean.sh
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="${ROOT}/build"

if [[ "${1:-}" == "--distclean" ]]; then
    echo "[clean] distclean: removing ${BUILD_DIR} entirely"
    rm -rf "${BUILD_DIR}"
    echo "[clean] done."
else
    echo "[clean] cleaning artifacts (keeping sources) ..."
    # keep downloaded tarballs and linux/busybox sources, remove outputs
    rm -rf "${BUILD_DIR}/iso"
    rm -rf "${BUILD_DIR}/initramfs-staging"
    rm -rf "${BUILD_DIR}/busybox-root"
    rm -f  "${BUILD_DIR}/bzImage"
    rm -f  "${BUILD_DIR}/initramfs.cpio.gz"
    rm -f  "${BUILD_DIR}"/toyium-*.iso
    # keep linux-*/.config and busybox-*/.config
    echo "[clean] kept sources in ${BUILD_DIR}/ (use --distclean to nuke all)"
    echo "[clean] done."
fi
