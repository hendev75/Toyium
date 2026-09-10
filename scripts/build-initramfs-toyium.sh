#!/usr/bin/env bash
# Toyium minimal initramfs assembly (run inside Linux/WSL)
# Builds /init from src/toyium.c, packs initramfs.cpio.gz.
# Usage: build-initramfs-toyium.sh <project-dir>
set -euo pipefail

ROOT="$1"
OUT_DIR="${ROOT}/build"
STAGE="${OUT_DIR}/initramfs-stage"
SRC="${ROOT}/src/toyium.c"

rm -rf "${STAGE}"
mkdir -p "${STAGE}"

echo "[initramfs] compiling /init"
gcc -Os -ffreestanding -nostdlib -static -fno-stack-protector \
    -fno-pie -no-pie -mno-red-zone -Wall \
    -o "${STAGE}/init" "${SRC}"
chmod 755 "${STAGE}/init"

echo "[initramfs] staging root"
mkdir -p "${STAGE}"/{proc,sys,dev,tmp,run,home,bin,etc}
ln -sf /init "${STAGE}/bin/toyls"
ln -sf /init "${STAGE}/bin/toycd"
printf 'toyium\n' > "${STAGE}/etc/hostname"

echo "[initramfs] packing"
(
  cd "${STAGE}"
  find . -print0 | cpio --null -o --format=newc 2>/dev/null | gzip -9 > "${OUT_DIR}/initramfs.cpio.gz"
)

ls -la "${OUT_DIR}/initramfs.cpio.gz"
echo "[initramfs] done"
