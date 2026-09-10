# Toyium OS — Dockerfile (reproducible builder)
# Build ISO without polluting host:
#   docker build -t toyium-builder .
#   docker run --rm -it -v ${PWD}:/toyium -w /toyium toyium-builder make all
#   docker run --rm -it -v ${PWD}:/toyium -w /toyium toyium-builder make run-nographic
FROM ubuntu:24.04

ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y \
    build-essential bc bison flex libelf-dev libssl-dev libncurses-dev \
    cpio ccache qemu-system-x86 xorriso grub-pc-bin grub-efi-amd64-bin mtools \
    wget curl git gcc make perl rsync dosfstools kmod \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /toyium
COPY . /toyium

# Make scripts executable (in case of Windows CRLF checkout)
RUN chmod +x scripts/*.sh overlay/init overlay/etc/init.d/rcS 2>/dev/null || true

CMD ["bash"]
