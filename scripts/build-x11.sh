#!/usr/bin/env bash
# Build a real X11 userland with Buildroot.
# Boots the Buildroot stock qemu_x86_64 kernel (full VT/DRM) with the X11
# rootfs, so Xorg can acquire a real VT and scan out.
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
KFRAGMENT="/mnt/c/Users/gws/Desktop/os/board/toyium/x11/linux-fbdev.fragment"
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
BR2_LINUX_KERNEL=y
BR2_LINUX_KERNEL_CUSTOM_VERSION=y
BR2_LINUX_KERNEL_CUSTOM_VERSION_VALUE="6.12.27"
BR2_LINUX_KERNEL_USE_CUSTOM_CONFIG=y
BR2_LINUX_KERNEL_CUSTOM_CONFIG_FILE="board/qemu/x86_64/linux.config"
BR2_LINUX_KERNEL_NEEDS_HOST_LIBELF=y
BR2_LINUX_KERNEL_CONFIG_FRAGMENT_FILES="${KFRAGMENT}"
BR2_PACKAGE_XORG7=y
BR2_PACKAGE_XSERVER_XORG_SERVER=y
BR2_PACKAGE_XSERVER_XORG_SERVER_MODULAR=y
BR2_PACKAGE_XAPP_XINIT=y
BR2_PACKAGE_OPENBOX=y
BR2_PACKAGE_XTERM=y
BR2_PACKAGE_XDRIVER_XF86_INPUT_EVDEV=y
BR2_PACKAGE_EVTEST=y
BR2_PACKAGE_XAPP_XEV=y
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

# Enable the Buildroot stock kernel on rebuilds too.
sed -i 's/^# BR2_LINUX_KERNEL is not set/BR2_LINUX_KERNEL=y/' "${OUT}/.config"
grep -q '^BR2_LINUX_KERNEL=y' "${OUT}/.config" || printf '%s\n' 'BR2_LINUX_KERNEL=y' >> "${OUT}/.config"
for kv in \
    'BR2_LINUX_KERNEL_CUSTOM_VERSION=y' \
    'BR2_LINUX_KERNEL_CUSTOM_VERSION_VALUE="6.12.27"' \
    'BR2_LINUX_KERNEL_USE_CUSTOM_CONFIG=y' \
    'BR2_LINUX_KERNEL_CUSTOM_CONFIG_FILE="board/qemu/x86_64/linux.config"' \
    'BR2_LINUX_KERNEL_NEEDS_HOST_LIBELF=y' ; do
    grep -q "^${kv%%=*}=" "${OUT}/.config" || printf '%s\n' "$kv" >> "${OUT}/.config"
done

# DRM fbdev-emulation fragment so bochs-drm exposes /dev/fb0 for the fbdev driver.
sed -i '/^BR2_LINUX_KERNEL_CONFIG_FRAGMENT_FILES=/d' "${OUT}/.config"
printf 'BR2_LINUX_KERNEL_CONFIG_FRAGMENT_FILES="%s"\n' "${KFRAGMENT}" >> "${OUT}/.config"

# Xterm musl pty fix is applied directly to the extracted source
# (see patch block below).

# Idempotent config fixes on rebuilds. Use the modular server so the
# X11R7 driver menu (evdev input) is selectable.
sed -i \
    -e 's/^BR2_PACKAGE_XSERVER_XORG_SERVER_KDRIVE=y/# BR2_PACKAGE_XSERVER_XORG_SERVER_KDRIVE is not set/' \
    -e 's/^# BR2_PACKAGE_XSERVER_XORG_SERVER_MODULAR is not set/BR2_PACKAGE_XSERVER_XORG_SERVER_MODULAR=y/' \
    "${OUT}/.config"
# evdev input needs udev: use eudev for dynamic /dev
sed -i 's/^BR2_ROOTFS_DEVICE_CREATION_DYNAMIC_DEVTMPFS=y/# BR2_ROOTFS_DEVICE_CREATION_DYNAMIC_DEVTMPFS is not set/' "${OUT}/.config"
grep -q '^BR2_ROOTFS_DEVICE_CREATION_DYNAMIC_EUDEV=y' "${OUT}/.config" || \
    printf '%s\n' 'BR2_ROOTFS_DEVICE_CREATION_DYNAMIC_EUDEV=y' >> "${OUT}/.config"
grep -q '^BR2_PACKAGE_XSERVER_XORG_SERVER_MODULAR=y' "${OUT}/.config" || \
    printf '%s\n' 'BR2_PACKAGE_XSERVER_XORG_SERVER_MODULAR=y' >> "${OUT}/.config"
for xpkg in BR2_PACKAGE_XDRIVER_XF86_INPUT_EVDEV BR2_PACKAGE_EVTEST BR2_PACKAGE_XAPP_XEV; do
    grep -q "^${xpkg}=y" "${OUT}/.config" || printf '%s\n' "${xpkg}=y" >> "${OUT}/.config"
done
# Remove legacy keyboard/mouse driver selections if present
sed -i '/^BR2_PACKAGE_XDRIVER_XF86_INPUT_KEYBOARD=y/d; /^BR2_PACKAGE_XDRIVER_XF86_INPUT_MOUSE=y/d' "${OUT}/.config"

# Diagnostic X clients (root painting + window tree).
for xpkg in BR2_PACKAGE_XAPP_XSETROOT BR2_PACKAGE_XAPP_XWININFO BR2_PACKAGE_XAPP_XDPYINFO; do
    grep -q "^${xpkg}=y" "${OUT}/.config" || printf '%s\n' "${xpkg}=y" >> "${OUT}/.config"
done

# Remove legacy input drivers (now handled by evdev) before olddefconfig
sed -i '/^BR2_PACKAGE_XDRIVER_XF86_INPUT_KEYBOARD/d; /^BR2_PACKAGE_XDRIVER_XF86_INPUT_MOUSE/d; /^BR2_PACKAGE_XDRIVER_XF86_INPUT_VOID/d' "${OUT}/.config" 2>/dev/null || true

# Ensure the post-build script is registered alongside Buildroot's own.
if ! grep -q "${POSTBUILD}" "${OUT}/.config"; then
    sed -i "s#^BR2_ROOTFS_POST_BUILD_SCRIPT=.*#BR2_ROOTFS_POST_BUILD_SCRIPT=\"board/qemu/x86_64/post-build.sh ${POSTBUILD}\"#" "${OUT}/.config"
fi

make -C "${BR}" O="${OUT}" olddefconfig
# Ensure xterm is extracted so the musl pty patch can be applied
make -C "${BR}" O="${OUT}" xterm-patch 2>/dev/null || true

# Patch xterm for musl pty (devpts newinstance + handshake). Applied
# directly to the extracted source so it survives incremental builds.
if [ -d "${OUT}/build/xterm-389" ]; then
    python3 - <<'PY'
import pathlib
import re
# ptyx.h: use PTS_DEVICE on musl linux
p = pathlib.Path("/root/toyium/buildroot-x11/build/xterm-389/ptyx.h")
if p.exists():
    t = p.read_text()
    orig = "#if defined(__osf__) || (defined(linux) && defined(__GLIBC__) && (__GLIBC__ >= 2) && (__GLIBC_MINOR__ >= 1)) || defined(__DragonFly__) || defined(__FreeBSD__)"
    repl = "#if defined(__osf__) || defined(linux) || defined(__DragonFly__) || defined(__FreeBSD__)"
    if orig in t:
        t = t.replace(orig, repl)
    orig2 = "#if (defined (__GLIBC__) && ((__GLIBC__ > 2) || (__GLIBC__ == 2) && (__GLIBC_MINOR__ >= 1)))"
    repl2 = "#if (defined (__GLIBC__) && ((__GLIBC__ > 2) || (__GLIBC__ == 2) && (__GLIBC_MINOR__ >= 1))) || defined(linux)"
    if orig2 in t:
        t = t.replace(orig2, repl2)
    t = t.replace("#define USE_PTS_DEVICE 1\n#define USE_OPENPTY 1", "#define USE_PTS_DEVICE 1")
    p.write_text(t)
# main.c: posix_openpt grant/unlock + EBADF + OPDEVTTY + ttydev debug
q = pathlib.Path("/root/toyium/buildroot-x11/build/xterm-389/main.c")
if q.exists():
    t = q.read_text()
    t = t.replace("#elif defined(HAVE_POSIX_OPENPT) && defined(HAVE_PTSNAME) && defined(HAVE_GRANTPT_PTY_ISATTY)",
                  "#elif defined(HAVE_POSIX_OPENPT) && defined(HAVE_PTSNAME)")
    t = t.replace('    if ((*pty = posix_openpt(O_RDWR)) >= 0) {\n\tchar *name = ptsname(*pty);',
                  '    if ((*pty = posix_openpt(O_RDWR)) >= 0) {\n\tgrantpt(*pty);\n\tunlockpt(*pty);\n\tchar *name = ptsname(*pty);')
    t = t.replace('    if (tty_got_hung || errno == ENXIO', '    if (1 || tty_got_hung || errno == ENXIO')
    t = t.replace('err\n\t    errno == ENOENT ||', 'err\n\t    errno == ENOENT || errno == EBADF ||')
    t = t.replace('SysError(ERROR_OPDEVTTY);', 'no_dev_tty = True; /* patched OPDEVTTY */')
    t = t.replace('perror("open ttydev");', 'fprintf(stderr, "open ttydev %s: %s\\n", ttydev, strerror(errno));')
    # Parent has no controlling tty under startx/getty: ttyfd is -1 here,
    # so skip ttyGetAttr on the dead fd instead of SysError(ERROR_TIOCGETP).
    t = t.replace('\t\tif (ttyGetAttr(ttyfd, &tio) == -1)\n\t\t    SysError(ERROR_TIOCGETP);',
                  '\t\tif (ttyfd >= 0 && ttyGetAttr(ttyfd, &tio) == -1)\n\t\t    SysError(ERROR_TIOCGETP);')
    # Child: the !USE_SYSV_PGRP foregrounding is skipped on Linux, leaving
    # the shell backgrounded (SIGTTIN stop) on musl + devpts newinstance.
    # Foreground our own process group explicitly.
    t = t.replace('#endif /* !USE_SYSV_PGRP */',
                  '#endif /* !USE_SYSV_PGRP */\n#if defined(USE_SYSV_PGRP) && defined(linux)\n\t    /* Toyium: foreground our pgrp on the pty slave */\n#ifdef TIOCSCTTY\n\t    ioctl(0, TIOCSCTTY, 0);\n#endif\n\t    tcsetpgrp(0, getpgrp());\n#endif')
    # musl does not predefine _POSIX_SOURCE, so the child takes getpid()
    # instead of setsid(): the slave never becomes the controlling terminal
    # and the shell is stopped (SIGTTIN). Force setsid on Linux.
    t = t.replace("#if defined(_POSIX_SOURCE) || defined(SVR4) || defined(__convex__) || defined(__SCO__) || defined(__QNX__)",
                  "#if defined(_POSIX_SOURCE) || defined(SVR4) || defined(__convex__) || defined(__SCO__) || defined(__QNX__) || defined(linux)")
    q.write_text(t)
PY
fi
# xtermcfg.h is generated by configure – patch it after
make -C "${BR}" O="${OUT}" xterm-configure 2>/dev/null || true
sed -i 's/#define OPT_PTY_HANDSHAKE 1/#define OPT_PTY_HANDSHAKE 0/' "${OUT}/build/xterm-389/xtermcfg.h" 2>/dev/null || true
# Rebuild xterm from the patched source every time: Buildroot will not
# recompile on source mtime alone, and a stale xterm binary is silent.
make -C "${BR}" O="${OUT}" xterm-rebuild 2>/dev/null || true

make -C "${BR}" O="${OUT}" -j"${JOBS}"

mkdir -p "${ROOT}/build"
IMAGE="${OUT}/images/rootfs.ext4"
[[ -e "${IMAGE}" ]] || IMAGE="${OUT}/images/rootfs.ext2"
cp -vL "${IMAGE}" "${ROOT}/build/toyium-x11-rootfs.ext4"

KIMAGE="${OUT}/images/bzImage"
if [[ -e "${KIMAGE}" ]]; then
    cp -vL "${KIMAGE}" "${ROOT}/build/toyium-x11-bzImage"
    echo "[x11] kernel: ${ROOT}/build/toyium-x11-bzImage"
fi
echo "[x11] rootfs: ${ROOT}/build/toyium-x11-rootfs.ext4"
