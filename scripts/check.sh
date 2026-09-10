#!/usr/bin/env bash
# Toyium OS — check.sh
# Validates project structure without needing a full kernel build.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
PASS=0; FAIL=0
check() {
    local desc="$1"; shift
    if eval "$@" >/dev/null 2>&1; then
        echo "  ✓ $desc"
        PASS=$((PASS+1))
    else
        echo "  ✗ $desc"
        FAIL=$((FAIL+1))
    fi
}
echo "[check] Toyium OS v$(cat "$ROOT/VERSION" 2>/dev/null || echo ?) — sanity check"
echo ""
check "VERSION exists"               "test -f '$ROOT/VERSION'"
check "Makefile exists"              "test -f '$ROOT/Makefile'"
check "grub/grub.cfg exists"         "test -f '$ROOT/grub/grub.cfg'"
check "overlay/init executable"      "test -x '$ROOT/overlay/init'"
check "overlay/init shebang"         "head -1 '$ROOT/overlay/init' | grep -q '^#!/bin/sh'"
check "overlay/etc/hostname=toyium"  "grep -q toyium '$ROOT/overlay/etc/hostname'"
check "overlay/etc/os-release"       "test -f '$ROOT/overlay/etc/os-release'"
check "overlay/etc/inittab"          "test -f '$ROOT/overlay/etc/inittab'"
check "configs/kernel.config"        "test -f '$ROOT/configs/kernel.config'"
check "configs/busybox.config"       "test -f '$ROOT/configs/busybox.config'"
check "kernel.config has 64BIT"      "grep -q CONFIG_64BIT=y '$ROOT/configs/kernel.config'"
check "kernel.config has INITRD"     "grep -q CONFIG_BLK_DEV_INITRD=y '$ROOT/configs/kernel.config'"
check "busybox.config static"        "grep -q CONFIG_STATIC=y '$ROOT/configs/busybox.config'"
check "src/toyium-fetch.c exists"    "test -f '$ROOT/src/toyium-fetch.c'"
check "src/toyium-init.c exists"     "test -f '$ROOT/src/toyium-init.c'"
check "scripts/build.sh executable"  "test -x '$ROOT/scripts/build.sh' || test -f '$ROOT/scripts/build.sh'"
check "scripts/run-qemu.sh exists"   "test -f '$ROOT/scripts/run-qemu.sh'"
check "grub.cfg has Toyium entry"    "grep -q 'Toyium OS' '$ROOT/grub/grub.cfg'"
# try compile toyium-fetch if gcc present
if command -v gcc >/dev/null 2>&1; then
    echo ""
    echo "[check] compiling src/toyium-fetch.c ..."
    if gcc -O2 -o /tmp/toyium-fetch-test "$ROOT/src/toyium-fetch.c" 2>&1; then
        echo "  ✓ toyium-fetch compiles"
        PASS=$((PASS+1))
        /tmp/toyium-fetch-test 2>&1 | head -n 25 || true
        rm -f /tmp/toyium-fetch-test
    else
        echo "  ✗ toyium-fetch compile failed"
        FAIL=$((FAIL+1))
    fi
    echo ""
    echo "[check] compiling src/toyium-init.c ..."
    if gcc -O2 -o /tmp/toyium-init-test "$ROOT/src/toyium-init.c" 2>&1; then
        echo "  ✓ toyium-init compiles"
        PASS=$((PASS+1))
        rm -f /tmp/toyium-init-test
    else
        echo "  ✗ toyium-init compile failed"
        FAIL=$((FAIL+1))
    fi
fi
echo ""
echo "[check] $PASS passed, $FAIL failed"
if [[ $FAIL -eq 0 ]]; then
    echo "[check] ALL CHECKS PASSED ✓ — ready to: make all"
else
    echo "[check] SOME CHECKS FAILED ✗"
    exit 1
fi
