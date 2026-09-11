#!/bin/sh
set -eu

TARGET_DIR="$1"

# Overlay files are checked out on Windows, so strip CRLF. Xorg's config
# parser (and shell scripts) misbehave with \r line endings.
find "$TARGET_DIR" -type f \( -path '*/etc/*' -o -name '*.sh' \) -exec sed -i 's/\r$//' {} + 2>/dev/null || true

# Buildroot's default fstab omits /dev. Without devtmpfs there is no
# /dev/fb0, /dev/dri/card0, /dev/tty1 or /dev/input/*, so Xorg cannot start.
if ! grep -q '[[:space:]]/dev[[:space:]]' "$TARGET_DIR/etc/fstab"; then
    printf 'devtmpfs\t/dev\tdevtmpfs\tmode=0755,nosuid\t0\t0\n' >> "$TARGET_DIR/etc/fstab"
fi

# With DEVPTS_MULTIPLE_INSTANCES always-on (Linux >= 4.7), the global
# /dev/ptmx allocates ptys in a different instance than a plain /dev/pts
# mount, so opening the slave gives EIO (xterm hangs in "open ttydev").
# Mount devpts as a new instance and point /dev/ptmx at its multiplexer.
sed -i 's#^\(devpts[[:space:]]*/dev/pts[[:space:]]*devpts[[:space:]]*\)[^[:space:]]*#\1newinstance,gid=5,mode=620,ptmxmode=0666#' \
    "$TARGET_DIR/etc/fstab"
if ! grep -q 'ln -sf pts/ptmx /dev/ptmx' "$TARGET_DIR/etc/inittab"; then
    sed -i '/^::sysinit:\/bin\/mount -a$/a ::sysinit:/bin/ln -sf pts/ptmx /dev/ptmx' \
        "$TARGET_DIR/etc/inittab"
fi

mkdir -p "$TARGET_DIR/sbin" "$TARGET_DIR/tmp/runtime-root" "$TARGET_DIR/root"

# Wrapper run by the tty1 getty. Running startx here means Xorg inherits a real
# controlling terminal, which it needs to grab the VT and scan out.
cat > "$TARGET_DIR/sbin/toyium-x11-shell" <<'EOF'
#!/bin/sh
export HOME=/root
export DISPLAY=:0
export XDG_RUNTIME_DIR=/tmp/runtime-root
export LANG=C
export TERM=xterm
mkdir -p "$XDG_RUNTIME_DIR"
chmod 700 "$XDG_RUNTIME_DIR"
cd /root || cd /
exec startx /root/.xinitrc -- :0 vt1 -keeptty >/dev/ttyS0 2>&1
EOF
chmod 755 "$TARGET_DIR/sbin/toyium-x11-shell"

# X session: window manager + terminals.
cat > "$TARGET_DIR/root/.xinitrc" <<'EOF'
#!/bin/sh
export LANG=C
xterm -fn fixed -bg white -fg black \
      -geometry 90x28+40+40 -title "Toyium X11 Terminal" &
xterm -fn fixed -bg black -fg white \
      -geometry 90x28+120+120 -title "Toyium Shell" &
exec openbox
EOF
chmod 755 "$TARGET_DIR/root/.xinitrc"

# BusyBox getty has no --autologin. Use -n (no name prompt) + -l <program>.
# tty1 runs the X session; ttyS0 stays as a login-free serial diagnostic shell.
sed -i '/tty1::respawn:/d' "$TARGET_DIR/etc/inittab"
sed -i '/ttyS0::respawn:/d' "$TARGET_DIR/etc/inittab"
cat >> "$TARGET_DIR/etc/inittab" <<'EOF'
ttyS0::respawn:/sbin/getty -L -n -l /bin/sh ttyS0 115200 vt100
tty1::respawn:/sbin/getty -L -n -l /sbin/toyium-x11-shell tty1 0 linux
EOF

# Single controlled X path above; drop Buildroot's standalone Xorg service and
# any stale debug copies left in the target from earlier experiments.
rm -f "$TARGET_DIR/etc/init.d/S40xorg" "$TARGET_DIR/etc/init.d/S41xclients" \
      "$TARGET_DIR/etc/init.d/S99x11"
# Stale xorg.conf.d snippets from earlier experiments override /etc/X11/xorg.conf.
rm -f "$TARGET_DIR/etc/X11/xorg.conf.d/10-toyium.conf"
