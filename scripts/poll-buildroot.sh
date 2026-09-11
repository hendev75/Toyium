#!/bin/sh
echo "make=$(ps ax | grep -E 'make -C /root/buildroot' | grep -v grep | wc -l)"
if [ -f /root/toyium/buildroot-x11/images/rootfs.ext4 ]; then
    ls -lh /root/toyium/buildroot-x11/images/rootfs.ext4
else
    echo "rootfs=not-ready"
fi
if [ -f /root/toyium/buildroot-x11/build.log ]; then
    tail -2 /root/toyium/buildroot-x11/build.log
fi
