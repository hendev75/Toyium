// SPDX-License-Identifier: GPL-2.0-only
/*
 * inode.c: the Toyium in-memory filesystem (toyfs)
 *
 * toyfs is a small, self-contained read-write filesystem that keeps all of
 * its data in the page cache / VFS caches. It is the native filesystem of
 * Toyium OS: files and directories live in RAM and are reachable through the
 * normal VFS paths once mounted.
 *
 * Modeled on the classic "simple" in-memory filesystem pattern.
 */

#include <linux/fs.h>
#include <linux/pagemap.h>
#include <linux/highmem.h>
#include <linux/time.h>
#include <linux/init.h>
#include <linux/string.h>
#include <linux/backing-dev.h>
#include <linux/sched.h>
#include <linux/parser.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/fs_context.h>
#include <linux/seq_file.h>
#include "internal.h"

#define TOYFS_MAGIC 0x746f7966 /* "toyf" */

#define TOYFS_DEFAULT_MODE 0755

static const struct super_operations toyfs_ops;
static const struct inode_operations toyfs_dir_inode_operations;

static struct inode *toyfs_get_inode(struct super_block *sb,
				     const struct inode *dir, umode_t mode,
				     dev_t dev)
{
	struct inode *inode = new_inode(sb);

	if (inode) {
		inode->i_ino = get_next_ino();
		inode_init_owner(&nop_mnt_idmap, inode, dir, mode);
		inode->i_mapping->a_ops = &ram_aops;
		mapping_set_gfp_mask(inode->i_mapping, GFP_HIGHUSER);
		mapping_set_unevictable(inode->i_mapping);
		simple_inode_init_ts(inode);
		switch (mode & S_IFMT) {
		default:
			init_special_inode(inode, mode, dev);
			break;
		case S_IFREG:
			inode->i_op = &toyfs_file_inode_operations;
			inode->i_fop = &toyfs_file_operations;
			break;
		case S_IFDIR:
			inode->i_op = &toyfs_dir_inode_operations;
			inode->i_fop = &simple_dir_operations;

			/* directory inodes start off with i_nlink == 2 (for ".") */
			inc_nlink(inode);
			break;
		case S_IFLNK:
			inode->i_op = &page_symlink_inode_operations;
			inode_nohighmem(inode);
			break;
		}
	}
	return inode;
}

/*
 * File creation: allocate an inode and let the VFS caches hold the data.
 */
static int toyfs_mknod(struct mnt_idmap *idmap, struct inode *dir,
		       struct dentry *dentry, umode_t mode, dev_t dev)
{
	struct inode *inode = toyfs_get_inode(dir->i_sb, dir, mode, dev);
	int error = -ENOSPC;

	if (inode) {
		error = security_inode_init_security(inode, dir,
						     &dentry->d_name, NULL,
						     NULL);
		if (error) {
			iput(inode);
			goto out;
		}

		d_make_persistent(dentry, inode);
		error = 0;
		inode_set_mtime_to_ts(dir, inode_set_ctime_current(dir));
	}
out:
	return error;
}

static struct dentry *toyfs_mkdir(struct mnt_idmap *idmap, struct inode *dir,
				  struct dentry *dentry, umode_t mode)
{
	int retval = toyfs_mknod(&nop_mnt_idmap, dir, dentry, mode, 0);

	if (!retval)
		inc_nlink(dir);
	return ERR_PTR(retval);
}

static int toyfs_create(struct mnt_idmap *idmap, struct inode *dir,
			struct dentry *dentry, umode_t mode)
{
	return toyfs_mknod(&nop_mnt_idmap, dir, dentry, mode | S_IFREG, 0);
}

static int toyfs_symlink(struct mnt_idmap *idmap, struct inode *dir,
			 struct dentry *dentry, const char *symname)
{
	struct inode *inode;
	int error = -ENOSPC;

	inode = toyfs_get_inode(dir->i_sb, dir, S_IFLNK | S_IRWXUGO, 0);
	if (inode) {
		int l = strlen(symname) + 1;

		error = security_inode_init_security(inode, dir,
						     &dentry->d_name, NULL,
						     NULL);
		if (error) {
			iput(inode);
			goto out;
		}

		error = page_symlink(inode, symname, l);
		if (!error) {
			d_make_persistent(dentry, inode);
			inode_set_mtime_to_ts(dir, inode_set_ctime_current(dir));
		} else {
			iput(inode);
		}
	}
out:
	return error;
}

static int toyfs_tmpfile(struct mnt_idmap *idmap, struct inode *dir,
			 struct file *file, umode_t mode)
{
	struct inode *inode;
	int error;

	inode = toyfs_get_inode(dir->i_sb, dir, mode, 0);
	if (!inode)
		return -ENOSPC;

	error = security_inode_init_security(inode, dir,
					     &file_dentry(file)->d_name, NULL,
					     NULL);
	if (error) {
		iput(inode);
		goto out;
	}

	d_tmpfile(file, inode);
out:
	return finish_open_simple(file, error);
}

static const struct inode_operations toyfs_dir_inode_operations = {
	.create		= toyfs_create,
	.lookup		= simple_lookup,
	.link		= simple_link,
	.unlink		= simple_unlink,
	.symlink	= toyfs_symlink,
	.mkdir		= toyfs_mkdir,
	.rmdir		= simple_rmdir,
	.mknod		= toyfs_mknod,
	.rename		= simple_rename,
	.tmpfile	= toyfs_tmpfile,
};

static int toyfs_show_options(struct seq_file *m, struct dentry *root)
{
	return 0;
}

static const struct super_operations toyfs_ops = {
	.statfs		= simple_statfs,
	.drop_inode	= inode_just_drop,
	.show_options	= toyfs_show_options,
};

static int toyfs_fill_super(struct super_block *sb, struct fs_context *fc)
{
	struct inode *inode;

	sb->s_maxbytes		= MAX_LFS_FILESIZE;
	sb->s_blocksize		= PAGE_SIZE;
	sb->s_blocksize_bits	= PAGE_SHIFT;
	sb->s_magic		= TOYFS_MAGIC;
	sb->s_op		= &toyfs_ops;
	sb->s_d_flags		= DCACHE_DONTCACHE;
	sb->s_time_gran		= 1;

	inode = toyfs_get_inode(sb, NULL, S_IFDIR | TOYFS_DEFAULT_MODE, 0);
	sb->s_root = d_make_root(inode);
	if (!sb->s_root)
		return -ENOMEM;

	return 0;
}

static int toyfs_get_tree(struct fs_context *fc)
{
	return get_tree_nodev(fc, toyfs_fill_super);
}

static const struct fs_context_operations toyfs_context_ops = {
	.get_tree	= toyfs_get_tree,
};

static int toyfs_init_fs_context(struct fs_context *fc)
{
	fc->ops = &toyfs_context_ops;
	return 0;
}

static void toyfs_kill_sb(struct super_block *sb)
{
	kill_anon_super(sb);
}

static struct file_system_type toyfs_fs_type = {
	.name		= "toyfs",
	.init_fs_context = toyfs_init_fs_context,
	.kill_sb	= toyfs_kill_sb,
	.fs_flags	= FS_USERNS_MOUNT,
};

static int __init init_toyfs_fs(void)
{
	return register_filesystem(&toyfs_fs_type);
}
fs_initcall(init_toyfs_fs);
