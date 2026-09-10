#!/usr/bin/env bash
# Toyium minimal initramfs assembly (run inside Linux/WSL)
# Builds /init from src/toyium.c, packs initramfs.cpio.gz.
# Usage: build-initramfs-toyium.sh <project-dir>
set -euo pipefail

ROOT="$(cd "$1" && pwd)"
OUT_DIR="${ROOT}/build"
STAGE="${OUT_DIR}/initramfs-stage"
SRC="${ROOT}/src/toyium.c"

rm -rf "${STAGE}"
mkdir -p "${STAGE}"

CCFLAGS="-Os -ffreestanding -nostdlib -static -fno-stack-protector -fno-pie -no-pie -mno-red-zone -Wall -Wno-unused-function -Wno-misleading-indentation -I${ROOT}/src"

echo "[initramfs] compiling /init (shell)"
gcc $CCFLAGS -o "${STAGE}/init" "${ROOT}/src/toyium.c"
chmod 755 "${STAGE}/init"

echo "[initramfs] staging root"
mkdir -p "${STAGE}"/{proc,sys,dev,tmp,run,home,bin,etc}
printf 'toyium\n' > "${STAGE}/etc/hostname"

echo "[initramfs] compiling userland programs"
for prog in toywm toyterm toyps; do
    gcc $CCFLAGS -o "${STAGE}/bin/${prog}" "${ROOT}/src/${prog}.c"
    chmod 755 "${STAGE}/bin/${prog}"
    echo "  -> /bin/${prog}"
done

echo "[initramfs] packing"
(
  cd "${STAGE}"
  find . -print0 | cpio --null -o --format=newc 2>/dev/null | gzip -9 > "${OUT_DIR}/initramfs.cpio.gz"
)

ls -la "${OUT_DIR}/initramfs.cpio.gz"
echo "[initramfs] done"
