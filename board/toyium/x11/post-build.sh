#!/bin/sh
set -eu

TARGET_DIR="$1"

# Overlay files are checked out on Windows, so strip CRLF. Xorg's config
# parser (and shell scripts) misbehave with \r line endings.
find "$TARGET_DIR" -type f \( -path '*/etc/*' -o -name '*.sh' \) -exec sed -i 's/\r$//' {} + 2>/dev/null || true

# Buildroot's default fstab omits /dev. Without devtmpfs there is no
# /dev/dri/card0 or /dev/input/*, so Xorg cannot start.
if ! grep -q '[[:space:]]/dev[[:space:]]' "$TARGET_DIR/etc/fstab"; then
    printf 'devtmpfs\t/dev\tdevtmpfs\tmode=0755,nosuid\t0\t0\n' >> "$TARGET_DIR/etc/fstab"
fi

mkdir -p "$TARGET_DIR/tmp/runtime-root"

# Serial console stays available as a diagnostic root shell.
# (BusyBox getty has no --autologin; -n + -l /bin/sh gives a login-free shell.)
sed -i '/ttyS0::respawn:/d' "$TARGET_DIR/etc/inittab"
sed -i '/tty1::respawn:/d' "$TARGET_DIR/etc/inittab"
printf 'ttyS0::respawn:/sbin/getty -L -n -l /bin/sh ttyS0 115200 vt100\n' \
    >> "$TARGET_DIR/etc/inittab"

# S40xorg (shipped by Buildroot) starts Xorg on vt01. Start the window manager
# and terminals once the X socket appears.
cat > "$TARGET_DIR/etc/init.d/S41xclients" <<'EOF'
#!/bin/sh
case "$1" in
    start)
        # Wait for the X server started by S40xorg.
        for i in $(seq 1 30); do
            [ -S /tmp/.X11-unix/X0 ] && break
            sleep 1
        done
        sleep 2
        export DISPLAY=:0
        export HOME=/root
        export XDG_RUNTIME_DIR=/tmp/runtime-root
        export LANG=C
        mkdir -p "$XDG_RUNTIME_DIR"
        openbox >/dev/ttyS0 2>&1 &
        xterm -bg white -fg black -geometry 90x28+40+40 \
              -title "Toyium X11 Terminal" >/dev/ttyS0 2>&1 &
        xterm -bg black -fg white -geometry 90x28+120+120 \
              -title "Toyium Shell" >/dev/ttyS0 2>&1 &
        ;;
esac
exit 0
EOF
chmod 755 "$TARGET_DIR/etc/init.d/S41xclients"
