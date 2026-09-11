#!/bin/sh
set -eu
mkdir -p /root/toyium/buildroot-x11
setsid nohup bash /mnt/c/Users/gws/Desktop/os/scripts/build-x11.sh \
  >/root/toyium/buildroot-x11/build.log 2>&1 </dev/null &
echo "started buildroot pid $!"
