#!/usr/bin/env bash
# Toyium OS — build-initramfs.sh
# Assembles initramfs.cpio.gz from busybox-root + overlay/
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="${ROOT}/build"
OVERLAY="${ROOT}/overlay"
BUSYBOX_ROOT="${BUILD_DIR}/busybox-root"
STAGING="${BUILD_DIR}/initramfs-staging"
OUT="${BUILD_DIR}/initramfs.cpio.gz"

if [[ ! -d "${BUSYBOX_ROOT}" ]]; then
    echo "[initramfs] ERROR: ${BUSYBOX_ROOT} not found. Run build-busybox.sh first." >&2
    exit 1
fi

echo "[initramfs] staging -> ${STAGING}"
rm -rf "${STAGING}"
mkdir -p "${STAGING}"

# 1. copy busybox root
cp -a "${BUSYBOX_ROOT}/"* "${STAGING}/"

# 2. overlay (etc, root, custom bin, etc.)
if [[ -d "${OVERLAY}" ]]; then
    echo "[initramfs] applying overlay/"
    cp -a "${OVERLAY}/"* "${STAGING}/" 2>/dev/null || true
    # ensure overlay/bin exists copy
    if [[ -d "${OVERLAY}/bin" ]]; then
        mkdir -p "${STAGING}/bin"
        cp -a "${OVERLAY}/bin/"* "${STAGING}/bin/" 2>/dev/null || true
    fi
fi

# 3. ensure critical dirs
mkdir -p "${STAGING}"/{proc,sys,dev,tmp,run,var/log,etc,root,home,mnt,usr/bin,usr/sbin}

# 4. ensure /init is executable
if [[ -f "${STAGING}/init" ]]; then
    chmod +x "${STAGING}/init"
else
    echo "[initramfs] WARNING: no /init found in overlay — creating fallback"
    cat > "${STAGING}/init" <<'INIT'
#!/bin/sh
mount -t proc none /proc
mount -t sysfs none /sys
mount -t devtmpfs none /dev
echo "Toyium OS fallback init"
exec /bin/sh
INIT
    chmod +x "${STAGING}/init"
fi

# 5. compile toyium-fetch if present and not already in overlay
if [[ -f "${ROOT}/src/toyium-fetch.c" && ! -f "${STAGING}/bin/toyium-fetch" ]]; then
    if command -v gcc >/dev/null; then
        echo "[initramfs] compiling toyium-fetch ..."
        mkdir -p "${STAGING}/bin"
        # try static first, fallback to dynamic
        if gcc -static -O2 -o "${STAGING}/bin/toyium-fetch" "${ROOT}/src/toyium-fetch.c" 2>/dev/null; then
            echo "[initramfs] toyium-fetch static ✓"
        elif gcc -O2 -o "${STAGING}/bin/toyium-fetch" "${ROOT}/src/toyium-fetch.c" 2>/dev/null; then
            echo "[initramfs] toyium-fetch dynamic (non-static) ✓"
        else
            echo "[initramfs] WARNING: failed to compile toyium-fetch"
        fi
        chmod +x "${STAGING}/bin/toyium-fetch" 2>/dev/null || true
    fi
fi

# 6. busybox symlinks (if not already created by install)
if [[ ! -L "${STAGING}/bin/sh" ]]; then
    echo "[initramfs] creating busybox symlinks ..."
    # use busybox --install if possible, otherwise manual
    if [[ -x "${STAGING}/bin/busybox" ]]; then
        # busybox --install needs staging as cwd
        (cd "${STAGING}" && bin/busybox --install -s 2>/dev/null) || true
    fi
fi

# 7. permissions
chmod 755 "${STAGING}/init"
chmod -R 755 "${STAGING}/bin" 2>/dev/null || true
chmod -R 755 "${STAGING}/sbin" 2>/dev/null || true
mkdir -p "${STAGING}/dev"
# device nodes are provided by devtmpfs at boot, no need to mknod here

# 8. create cpio
echo "[initramfs] packing cpio.gz -> ${OUT}"
(
    cd "${STAGING}"
    find . -print0 | cpio --null -ov --format=newc 2>/dev/null | gzip -9 > "${OUT}"
)

SIZE="$(du -h "${OUT}" | cut -f1)"
COUNT="$(find "${STAGING}" | wc -l)"
echo "[initramfs] -> ${OUT} (${SIZE}, ${COUNT} files) ✓"
echo "[initramfs] done."
