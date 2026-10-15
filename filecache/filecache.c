#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/version.h>
#include <linux/fs.h>
#include <linux/spinlock.h>
#include <linux/kallsyms.h> // kallsyms_lookup_name -> iterate_supers
#include <linux/dcache.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>

static void (*kernel_iterate_supers)(void (*f)(struct super_block *, void *), void *arg);
#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 0, 0)
/* Steal the address of this spinlock from the kernel, don't init it*/
static spinlock_t *kernel_inode_sb_list_lock;
#endif

#if LINUX_VERSION_CODE > KERNEL_VERSION(4, 0, 0)
#define DENTRY_ALIAS d_u.d_alias
#else
#define DENTRY_ALIAS d_alias
#endif

// similar with __d_find_any_alias()
// can't use d_find_any_alias() as we have already hold inode lock.
struct dentry *dentry_from_inode(struct inode *inode)
{
	struct dentry *dentry = NULL;

	if (!inode)
		return NULL;

	if (hlist_empty(&inode->i_dentry))
		return NULL;

	dentry = hlist_entry(inode->i_dentry.first, struct dentry, DENTRY_ALIAS);

	dget(dentry);
	return dentry;
}

static void lock_sb_inode_list(struct super_block *sb)
{
#if LINUX_VERSION_CODE > KERNEL_VERSION(4, 0, 0)
	spin_lock(&sb->s_inode_list_lock);
#else
	spin_lock(kernel_inode_sb_list_lock);
#endif
}

static void unlock_sb_inode_list(struct super_block *sb)
{
#if LINUX_VERSION_CODE > KERNEL_VERSION(4, 0, 0)
	spin_unlock(&sb->s_inode_list_lock);
#else
	spin_unlock(kernel_inode_sb_list_lock);
#endif
}

char filename[PATH_MAX];

static void iterate_pagecache_sb(struct super_block *sb, void *m)
{
	struct inode *inode;
	struct dentry *dentry = NULL;
	unsigned long nrpages;
	char *tmp;

	lock_sb_inode_list(sb);
	list_for_each_entry(inode, &sb->s_inodes, i_sb_list) {
		spin_lock(&inode->i_lock);
		nrpages = inode->i_mapping->nrpages;
		if (nrpages) {
			dentry = dentry_from_inode(inode);
			if (dentry) {
				tmp = dentry_path_raw(dentry, filename, PATH_MAX);
				dput(dentry);
				if (!IS_ERR(tmp))
					seq_printf(m, "%8lu %s\n",
						   inode->i_mapping->nrpages,
						   tmp);
			} else {
				seq_printf(m, "%8lu %lu No Dentry\n",
					   inode->i_mapping->nrpages,
					   inode->i_ino);
			}
		}
		spin_unlock(&inode->i_lock);
	}

	unlock_sb_inode_list(sb);
}

int get_inode_sb_list_lock_addr(void)
{
#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 0, 0) 
	unsigned long lock;

	lock = kallsyms_lookup_name("inode_sb_list_lock");
	if (!lock)
		return -1;
	kernel_inode_sb_list_lock = (void *)lock;
#endif

	return 0;
}

static int filecache_proc_show(struct seq_file *m, void *v)
{
	kernel_iterate_supers(iterate_pagecache_sb, m);
	return 0;
}

#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 0, 0)
static int filecache_proc_open(struct inode *inode, struct file *file)
{
	return single_open(file, filecache_proc_show, NULL);
}

static const struct file_operations filecache_proc_ops = {
	.owner		= THIS_MODULE,
	.open		= filecache_proc_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,

};
#endif

static int __init filecache_init(void)
{
	unsigned long addr;

	addr = kallsyms_lookup_name("iterate_supers");
	if (!addr)
		return -1;

	kernel_iterate_supers = (void *)addr;
	if (get_inode_sb_list_lock_addr())
		return -1;

#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 0, 0)
	proc_create("filecache", 0, NULL, &filecache_proc_ops);
#else
	proc_create_single("filecache", 0, NULL, filecache_proc_show);
#endif

	return 0;
}

static void __exit filecache_exit(void)
{
	remove_proc_entry("filecache", NULL);
}

module_init(filecache_init);
module_exit(filecache_exit);
MODULE_LICENSE("GPL v2");
