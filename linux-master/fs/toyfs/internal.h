/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * internal.h: toyfs internal definitions
 *
 * toyfs - the Toyium OS in-memory filesystem.
 */

#ifndef _TOYFS_INTERNAL_H
#define _TOYFS_INTERNAL_H

#include <linux/fs.h>

extern const struct file_operations toyfs_file_operations;
extern const struct inode_operations toyfs_file_inode_operations;

#endif /* _TOYFS_INTERNAL_H */
