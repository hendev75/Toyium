#!/bin/sh
export XDG_RUNTIME_DIR=/tmp/runtime-root
mkdir -p "$XDG_RUNTIME_DIR"
chmod 700 "$XDG_RUNTIME_DIR"

openbox &
xterm -geometry 100x30+40+40 -title "Toyium X11 Terminal" &
exec xterm -geometry 100x30+80+100 -title "Toyium Shell"
