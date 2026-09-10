#!/usr/bin/env bash
# Toyium OS — run-qemu.sh
# Boot Toyium in QEMU (direct kernel or ISO)
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="${ROOT}/build"
VERSION="$(cat "${ROOT}/VERSION" 2>/dev/null || echo "0.1.0")"
KERNEL="${BUILD_DIR}/bzImage"
INITRAMFS="${BUILD_DIR}/initramfs.cpio.gz"
ISO="${BUILD_DIR}/toyium-${VERSION}-x86_64.iso"

MODE="direct"
NOGRAPHIC=0
KVM=""
MEM="512M"
SMP="2"

usage() {
    cat <<EOF
Usage: $0 [OPTIONS]

Modes:
  --direct        Boot with -kernel + -initrd (default, fastest)
  --iso           Boot ISO with -cdrom (tests GRUB bootloader)

Options:
  --nographic     Headless: -nographic -serial mon:stdio (serial console)
  --kvm           Force KVM (auto-detected otherwise)
  --no-kvm        Disable KVM
  --mem MEM       RAM (default: 512M)
  --smp N         CPUs (default: 2)
  -h, --help      Show help

Examples:
  $0 --direct
  $0 --iso
  $0 --direct --nographic
EOF
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --direct) MODE="direct"; shift ;;
        --iso) MODE="iso"; shift ;;
        --nographic) NOGRAPHIC=1; shift ;;
        --kvm) KVM="--enable-kvm"; shift ;;
        --no-kvm) KVM=""; shift ;;
        --mem) MEM="$2"; shift 2 ;;
        --smp) SMP="$2"; shift 2 ;;
        -h|--help) usage; exit 0 ;;
        *) echo "unknown arg: $1" >&2; usage; exit 1 ;;
    esac
done

# QEMU binary
QEMU="qemu-system-x86_64"
if ! command -v "${QEMU}" >/dev/null 2>&1; then
    echo "[qemu] ERROR: ${QEMU} not found. Install: sudo apt install qemu-system-x86" >&2
    exit 1
fi

# auto KVM
if [[ -z "${KVM}" && -c /dev/kvm && -r /dev/kvm && -w /dev/kvm ]]; then
    KVM="--enable-kvm"
    echo "[qemu] KVM available ✓"
else
    if [[ "${KVM}" == "--enable-kvm" && ! -c /dev/kvm ]]; then
        echo "[qemu] WARNING: /dev/kvm not found, disabling KVM"
        KVM=""
    fi
fi

QEMU_ARGS=(
    -m "${MEM}"
    -smp "${SMP}"
    -netdev user,id=u0,hostfwd=tcp::8080-:80
    -device virtio-net-pci,netdev=u0
)
if [[ -f "${BUILD_DIR}/disk.img" ]]; then
    QEMU_ARGS+=(-drive "file=${BUILD_DIR}/disk.img,if=virtio,format=raw")
fi

if [[ ${NOGRAPHIC} -eq 1 ]]; then
    QEMU_ARGS+=(-nographic -serial mon:stdio)
    APPEND_CONSOLE="console=ttyS0,115200n8"
else
    APPEND_CONSOLE="console=tty0 console=ttyS0,115200n8"
fi

if [[ "${MODE}" == "direct" ]]; then
    if [[ ! -f "${KERNEL}" ]]; then
        echo "[qemu] ERROR: kernel not found at ${KERNEL}" >&2
        echo "       run: make kernel  or  ./scripts/build-kernel.sh" >&2
        exit 1
    fi
    if [[ ! -f "${INITRAMFS}" ]]; then
        echo "[qemu] ERROR: initramfs not found at ${INITRAMFS}" >&2
        exit 1
    fi
    echo "[qemu] direct boot: ${KERNEL} + ${INITRAMFS}"
    echo "[qemu] cmd: ${QEMU} ${KVM} -kernel ${KERNEL} -initrd ${INITRAMFS} -append \"${APPEND_CONSOLE}\" ..."
    exec "${QEMU}" ${KVM} \
        -kernel "${KERNEL}" \
        -initrd "${INITRAMFS}" \
        -append "${APPEND_CONSOLE} ip=dhcp quiet loglevel=3" \
        "${QEMU_ARGS[@]}"

elif [[ "${MODE}" == "iso" ]]; then
    if [[ ! -f "${ISO}" ]]; then
        echo "[qemu] ERROR: ISO not found at ${ISO}" >&2
        echo "       run: make iso  or  ./scripts/build-iso.sh" >&2
        exit 1
    fi
    echo "[qemu] ISO boot: ${ISO}"
    # for nographic ISO, still pass console via boot params? GRUB already has console args in grub.cfg
    exec "${QEMU}" ${KVM} \
        -cdrom "${ISO}" \
        -boot d \
        "${QEMU_ARGS[@]}"
else
    echo "unknown mode: ${MODE}" >&2; exit 1
fi
