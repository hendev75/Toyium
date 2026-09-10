#!/usr/bin/env bash
# Toyium minimal kernel config + build (run inside a Linux/WSL environment)
# Usage: build-kernel-toyium.sh <kernel-src-dir>
set -euo pipefail

KDIR="$1"
JOBS="$(nproc 2>/dev/null || echo 4)"
OUT_DIR="$(cd "$(dirname "$0")/.." && pwd)/build"
mkdir -p "${OUT_DIR}"

cd "${KDIR}"

echo "[kernel] generating x86_64_defconfig"
make x86_64_defconfig

echo "[kernel] trimming to toyium minimal set"
./scripts/config \
    --set-str CONFIG_LOCALVERSION "-toyium" \
    -d LOCALVERSION_AUTO \
    -d MODULES -d MODULE_SIG \
    -d SOUND -d INPUT_JOYSTICK -d INPUT_TABLET -d INPUT_TOUCHSCREEN -d INPUT_MISC \
    -d HID -d USB -d SCSI -d ATA \
    -d I2C -d SPI -d MMC -d WATCHDOG -d HWMON -d THERMAL -d REGULATOR \
    -d STAGING -d DEBUG_INFO -d SECURITY -d WIRELESS -d BT -d BPF_SYSCALL \
    -d DEBUG_FS -d DEBUG_KERNEL

echo "[kernel] enabling console input (VGA keyboard)"
./scripts/config \
    -e INPUT -e INPUT_KEYBOARD -e KEYBOARD_ATKBD \
    -e SERIO -e SERIO_I8042 -e SERIO_LIBPS2

echo "[kernel] enabling minimal networking (virtio-net + DHCP autoconfig)"
./scripts/config \
    -e NET -e PACKET -e UNIX -e INET \
    -e IP_PNP -e IP_PNP_DHCP \
    -e NETDEVICES -e VIRTIO -e VIRTIO_PCI -e VIRTIO_NET \
    -e VIRTIO_BLK -e BLOCK

echo "[kernel] enabling graphics (bochs DRM -> /dev/fb0) and PS/2 mouse"
./scripts/config \
    -e FB -e DRM -e DRM_BOCHS -e DRM_FBDEV_EMULATION -e FRAMEBUFFER_CONSOLE \
    -e INPUT_MOUSE -e MOUSE_PS2 -e INPUT_MOUSEDEV -e INPUT_EVDEV

echo "[kernel] trimming heavyweight DRM drivers (keep bochs only)"
./scripts/config \
    -d DRM_I915 -d DRM_AMDGPU -d DRM_NOUVEAU -d DRM_RADEON -d DRM_XE \
    -d DRM_VMWGFX -d DRM_VIRTIO_GPU -d DRM_AST -d DRM_QXL -d DRM_MGAG200 \
    -d DRM_CIRRUS_QEMU -d DRM_GMA500 -d DRM_UDL -d DRM_VKMS -d DRM_LEGACY \
    -d DRM_DEBUG_MM

echo "[kernel] trimming netfilter (unused; source absent in this tree)"
./scripts/config \
    -d NETFILTER -d NETFILTER_XT_TARGET_TCPMSS -d NETFILTER_XT_MATCH_TCPMSS \
    -d NF_CONNTRACK -d NETFILTER_ADVANCED -d NET_SCHED -d NET_CLS

echo "[kernel] resolving deps (olddefconfig)"
make olddefconfig

echo "[kernel] sanity checks"
grep -q "^CONFIG_BLK_DEV_INITRD=y" .config || { echo "no BLK_DEV_INITRD" >&2; exit 1; }
grep -q "^CONFIG_DEVTMPFS=y" .config || { echo "no DEVTMPFS" >&2; exit 1; }
grep -q "^CONFIG_SERIAL_8250_CONSOLE=y" .config || { echo "no serial console" >&2; exit 1; }
grep -q "^CONFIG_BINFMT_ELF=y" .config || { echo "no binfmt_elf" >&2; exit 1; }
grep -q "^CONFIG_INET=y" .config || { echo "no INET" >&2; exit 1; }
grep -q "^CONFIG_VIRTIO_NET=y" .config || { echo "no VIRTIO_NET" >&2; exit 1; }
grep -q "^CONFIG_IP_PNP_DHCP=y" .config || { echo "no IP_PNP_DHCP" >&2; exit 1; }

echo "[kernel] building bzImage (-j${JOBS}) ..."
make -j"${JOBS}" bzImage

cp -v arch/x86/boot/bzImage "${OUT_DIR}/bzImage"
cp -v .config "${OUT_DIR}/kernel.config"
echo "[kernel] done: ${OUT_DIR}/bzImage"
