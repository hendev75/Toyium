# SPDX-License-Identifier: GPL-2.0-only
#
# Makefile for the Toyium in-memory filesystem (toyfs).
#

obj-y += toyfs.o

file-mmu-y := file-nommu.o
file-mmu-$(CONFIG_MMU) := file-mmu.o
toyfs-objs += inode.o $(file-mmu-y)
