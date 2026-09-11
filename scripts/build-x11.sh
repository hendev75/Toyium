#!/usr/bin/env bash
# Build a real X11 userland with Buildroot, using the existing Toyium kernel.
set -euo pipefail

# WSL interop can inject a Windows PATH containing spaces/newlines. Buildroot
# rejects that; use a clean Linux-only PATH for the entire build.
export PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin
export FORCE_UNSAFE_CONFIGURE=1

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BR="/root/buildroot"
OUT="/root/toyium/buildroot-x11"
OVERLAY="/mnt/c/Users/gws/Desktop/os/board/toyium/x11/overlay"
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
BR2_PACKAGE_XSERVER_XORG_SERVER_MODULAR=y
BR2_PACKAGE_XAPP_XINIT=y
BR2_PACKAGE_OPENBOX=y
BR2_PACKAGE_XTERM=y
BR2_PACKAGE_DEJAVU=y
BR2_PACKAGE_DEJAVU_MONO=y
BR2_PACKAGE_DEJAVU_SANS=y
BR2_PACKAGE_EUDEV=y
BR2_PACKAGE_LIBINPUT=y
BR2_PACKAGE_XDRIVER_XF86_INPUT_LIBINPUT=y
BR2_PACKAGE_UTIL_LINUX=y
BR2_PACKAGE_E2FSPROGS=y
BR2_TARGET_GENERIC_GETTY=y
BR2_TARGET_GENERIC_GETTY_PORT="ttyS0"
BR2_TARGET_GENERIC_GETTY_TERM="linux"
BR2_TARGET_GENERIC_GETTY_OPTIONS="--autologin root"
BR2_ROOTFS_OVERLAY="${OVERLAY}"
BR2_ROOTFS_POST_BUILD_SCRIPT="/mnt/c/Users/gws/Desktop/os/board/toyium/x11/post-build.sh"
BR2_TARGET_ROOTFS_EXT2=y
BR2_TARGET_ROOTFS_EXT2_4=y
BR2_TARGET_ROOTFS_EXT2_SIZE="512M"
BR2_TARGET_ROOTFS_TAR=n
EOF

make -C "${BR}" O="${OUT}" olddefconfig
fi

# Keep later rebuilds in sync when the overlay/post-build scripts change.
if ! grep -q '/mnt/c/Users/gws/Desktop/os/board/toyium/x11/post-build.sh' "${OUT}/.config"; then
    sed -i 's#^BR2_ROOTFS_POST_BUILD_SCRIPT=.*#BR2_ROOTFS_POST_BUILD_SCRIPT="board/qemu/x86_64/post-build.sh /mnt/c/Users/gws/Desktop/os/board/toyium/x11/post-build.sh"#' "${OUT}/.config"
    make -C "${BR}" O="${OUT}" olddefconfig
fi

# Xorg fbdev driver: uses /dev/fb0 directly, which is known-good on this kernel.
if ! grep -q '^BR2_PACKAGE_XDRIVER_XF86_VIDEO_FBDEV=y' "${OUT}/.config"; then
    printf '%s\n' 'BR2_PACKAGE_XDRIVER_XF86_VIDEO_FBDEV=y' >> "${OUT}/.config"
    make -C "${BR}" O="${OUT}" olddefconfig
fi

# kbd/mouse input drivers: no udev dependency, use /dev/tty0 and /dev/input/mice.
# Always strip stale/legacy input symbols that trip Buildroot's legacy-config
# check (xf86-input-keyboard was removed upstream; evdev/libinput need udev).
sed -i '/^BR2_PACKAGE_XDRIVER_XF86_INPUT_EVDEV/d; /^BR2_PACKAGE_XDRIVER_XF86_INPUT_LIBINPUT/d; /^BR2_PACKAGE_XDRIVER_XF86_INPUT_KEYBOARD/d; /^BR2_PACKAGE_LIBINPUT\b/d; /^BR2_PACKAGE_LIBINPUT_/d; /^BR2_PACKAGE_EUDEV/d' "${OUT}/.config"
grep -q '^BR2_PACKAGE_XDRIVER_XF86_INPUT_MOUSE=y' "${OUT}/.config" || \
    printf '%s\n' 'BR2_PACKAGE_XDRIVER_XF86_INPUT_MOUSE=y' >> "${OUT}/.config"
make -C "${BR}" O="${OUT}" olddefconfig
make -C "${BR}" O="${OUT}" -j"${JOBS}"

mkdir -p "${ROOT}/build"
IMAGE="${OUT}/images/rootfs.ext4"
if [[ ! -e "${IMAGE}" ]]; then
    IMAGE="${OUT}/images/rootfs.ext2"
fi
cp -vL "${IMAGE}" "${ROOT}/build/toyium-x11-rootfs.ext4"
echo "[x11] rootfs: ${ROOT}/build/toyium-x11-rootfs.ext4"
