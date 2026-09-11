#!/usr/bin/env bash
# Build a real X11 userland with Buildroot, using the existing Toyium kernel.
# Uses the KDrive Xfbdev server (draws straight to /dev/fb0, no VT/DRM needed).
set -euo pipefail

# WSL interop can inject a Windows PATH containing spaces/newlines. Buildroot
# rejects that; use a clean Linux-only PATH for the entire build.
export PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin
export FORCE_UNSAFE_CONFIGURE=1

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BR="/root/buildroot"
OUT="/root/toyium/buildroot-x11"
OVERLAY="/mnt/c/Users/gws/Desktop/os/board/toyium/x11/overlay"
POSTBUILD="/mnt/c/Users/gws/Desktop/os/board/toyium/x11/post-build.sh"
JOBS="$(nproc 2>/dev/null || echo 4)"

if [[ ! -f "${BR}/Makefile" ]]; then
    echo "[x11] Buildroot not found at ${BR}" >&2
    exit 1
fi

mkdir -p "${OUT}"

if [[ ! -f "${OUT}/.config" ]]; then
    make -C "${BR}" O="${OUT}" qemu_x86_64_defconfig
    cat >> "${OUT}/.config" <<EOF
BR2_TOOLCHAIN_BUILDROOT_MUSL=y
BR2_TOOLCHAIN_BUILDROOT_CXX=y
BR2_LINUX_KERNEL=n
BR2_PACKAGE_XORG7=y
BR2_PACKAGE_XSERVER_XORG_SERVER=y
BR2_PACKAGE_XSERVER_XORG_SERVER_KDRIVE=y
BR2_PACKAGE_XAPP_XINIT=y
BR2_PACKAGE_OPENBOX=y
BR2_PACKAGE_XTERM=y
BR2_PACKAGE_DEJAVU=y
BR2_PACKAGE_DEJAVU_MONO=y
BR2_PACKAGE_DEJAVU_SANS=y
BR2_PACKAGE_UTIL_LINUX=y
BR2_PACKAGE_E2FSPROGS=y
BR2_ROOTFS_OVERLAY="${OVERLAY}"
BR2_TARGET_ROOTFS_EXT2=y
BR2_TARGET_ROOTFS_EXT2_4=y
BR2_TARGET_ROOTFS_EXT2_SIZE="512M"
BR2_TARGET_ROOTFS_TAR=n
EOF
fi

# Idempotent config fixes on rebuilds.
sed -i \
    -e 's/^BR2_PACKAGE_XSERVER_XORG_SERVER_MODULAR=y/# BR2_PACKAGE_XSERVER_XORG_SERVER_MODULAR is not set/' \
    -e 's/^# BR2_PACKAGE_XSERVER_XORG_SERVER_KDRIVE is not set/BR2_PACKAGE_XSERVER_XORG_SERVER_KDRIVE=y/' \
    -e '/^BR2_PACKAGE_XDRIVER_XF86/d' \
    -e '/^BR2_PACKAGE_LIBINPUT\b/d' \
    -e '/^BR2_PACKAGE_LIBINPUT_/d' \
    -e '/^BR2_PACKAGE_EUDEV/d' \
    "${OUT}/.config"
grep -q '^BR2_PACKAGE_XSERVER_XORG_SERVER_KDRIVE=y' "${OUT}/.config" || \
    printf '%s\n' 'BR2_PACKAGE_XSERVER_XORG_SERVER_KDRIVE=y' >> "${OUT}/.config"

# Ensure the post-build script is registered alongside Buildroot's own.
if ! grep -q "${POSTBUILD}" "${OUT}/.config"; then
    sed -i "s#^BR2_ROOTFS_POST_BUILD_SCRIPT=.*#BR2_ROOTFS_POST_BUILD_SCRIPT=\"board/qemu/x86_64/post-build.sh ${POSTBUILD}\"#" "${OUT}/.config"
fi

make -C "${BR}" O="${OUT}" olddefconfig
make -C "${BR}" O="${OUT}" -j"${JOBS}"

mkdir -p "${ROOT}/build"
IMAGE="${OUT}/images/rootfs.ext4"
[[ -e "${IMAGE}" ]] || IMAGE="${OUT}/images/rootfs.ext2"
cp -vL "${IMAGE}" "${ROOT}/build/toyium-x11-rootfs.ext4"
echo "[x11] rootfs: ${ROOT}/build/toyium-x11-rootfs.ext4"
