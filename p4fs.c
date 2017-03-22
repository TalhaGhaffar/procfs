#include <linux/fs.h>
#include <linux/pagemap.h>
#include <linux/highmem.h>
#include <linux/time.h>
#include <linux/init.h>
#include <linux/string.h>
#include <linux/backing-dev.h>
#include <linux/ramfs.h>
#include <linux/sched.h>
#include <linux/parser.h>
#include <linux/magic.h>
#include <linux/slab.h>
#include <asm/uaccess.h>
#include <linux/module.h>
#include <linux/sched.h>

#include <linux/mm_types.h>

#define P4FS_MAGIC 0x18926734

#define TMPSIZE 20
#define BUFSIZE 500
#define PROC_INFO_SIZE 4000


/* Super block operations for p4fs superblock object */
static struct super_operations p4fs_super_operations = {
	.statfs		= simple_statfs,
	.drop_inode 	= generic_delete_inode,
};


static struct inode_operations p4fs_file_inode_operations = {
        .setattr        = simple_setattr,
        .getattr        = simple_getattr,
};

static const struct inode_operations p4fs_dir_inode_operations;

/*
 * Open a file.  All we have to do here is to copy over a
 * copy of the counter pointer so it's easier to get at.
 */
static int p4fs_open(struct inode *inode, struct file *filp)
{
        filp->private_data = inode->i_private;
        return 0;
}

/*
 * FIXME: 
 * 	From where am I called?
 *	Read data from the file
 *	Use file's name to get pid, use pid to get information from process' task_struct
 *	Copy the updated data to the user space.
 */
static ssize_t p4fs_read_file(struct file *file_p, char *buf, 
		size_t count, loff_t *offset)
{
	/* Should I only get the data of the file and dumo it or update the data ? */
	char tmp[BUFSIZE];
	int len;
	
	//char file_data[BUFSIZE];
	// = (char *) file_p->private_data;
	
	if (*offset > 0)
		return -1;
		
	len = snprintf(tmp, BUFSIZE, "%s\n", (char *) file_p->private_data);
	
	if (*offset > len)
		return 0;
	if (count > len - *offset)
		count = len - *offset;

	copy_to_user(buf, tmp + *offset, count);
	*offset += count;
	return count;
}


/*
 * Write a file.
 */
static ssize_t p4fs_write_file(struct file *filp, const char *buf,
                size_t count, loff_t *offset)
{
        int len;
	char tmp[TMPSIZE];
	char *data = (char *) filp->private_data;
	/*
	 * Only write the file from start.
	 */
        if (*offset != 0)
                return -EINVAL;
	/*
	 * Read the value from the user.
	 */
        if (count >= TMPSIZE)
                return -EINVAL;
        memset(tmp, 0, TMPSIZE);
        copy_from_user(tmp, buf, count);
	
	/* Write the data */
	len = snprintf(data, BUFSIZE, "%s", (char *) tmp);	
        return len;
}



/*
 * file operations structure.
 * FIXME: What do I actually need here?
 */
static struct file_operations p4fs_file_operations = {
        .open  		= p4fs_open,
        .read   	= p4fs_read_file,
        .write  	= p4fs_write_file,	
	.read_iter      = generic_file_read_iter,
        .write_iter     = generic_file_write_iter,
        .mmap           = generic_file_mmap,
        .fsync          = noop_fsync,
        .splice_read    = generic_file_splice_read,
        .splice_write   = iter_file_splice_write,
        .llseek         = generic_file_llseek,
};

/*
 * FIXME: Write documentation
 * @sb: Superblock
 * @dir: Inode
 * @mode: Mode for the new inode
 */
struct inode *p4fs_get_inode(struct super_block *sb, 
				const struct inode *dir, umode_t mode) {
	struct inode *n_inode = new_inode(sb);
	
	if (n_inode) {
		n_inode->i_ino = get_next_ino();
		inode_init_owner(n_inode, dir, mode);
		n_inode->i_atime = n_inode->i_mtime = n_inode->i_ctime = CURRENT_TIME;
		n_inode->i_blocks = 0;
		
		switch (mode & S_IFMT) {	/* switch on file type code extracted from mode */
                case S_IFREG:	/* reguler file */
                        n_inode->i_op = &p4fs_file_inode_operations;
                        n_inode->i_fop = &p4fs_file_operations;
                        break;
                case S_IFDIR:	/* directory */
                        n_inode->i_op = &p4fs_dir_inode_operations;
                        n_inode->i_fop = &simple_dir_operations;
                        inc_nlink(n_inode);
                        break;
                case S_IFLNK:	/* symbolic link */
                        n_inode->i_op = &page_symlink_inode_operations;
                        break;
                }
	}
	return n_inode;
}



/*
 * File creation. Allocate an inode, and we're done..
 */
/* SMP-safe */
static int p4fs_mknod(struct inode *dir, struct dentry *dentry, 
			umode_t mode, dev_t dev)
{
        struct inode * inode = p4fs_get_inode(dir->i_sb, dir, mode);

        if (inode) {
                d_instantiate(dentry, inode);
                dget(dentry);    
                dir->i_mtime = dir->i_ctime = CURRENT_TIME;
		return 0;
        }
        return -ENOSPC;
}

static int p4fs_mkdir(struct inode * dir, struct dentry * dentry, umode_t mode)
{
        int retval = p4fs_mknod(dir, dentry, mode | S_IFDIR, 0);
        if (!retval)
                inc_nlink(dir);	/*FIXME: Already done in p4fs_get_inode */
        return retval;
}

static int p4fs_create(struct inode *dir, struct dentry *dentry, umode_t mode, bool excl)
{
        return p4fs_mknod(dir, dentry, mode | S_IFREG, 0);
}

static const struct inode_operations p4fs_dir_inode_operations = {
        .create         = p4fs_create,
        .lookup         = simple_lookup,
        .link           = simple_link,
        .unlink         = simple_unlink,
//        .symlink        = ramfs_symlink,
        .mkdir          = p4fs_mkdir,
        .rmdir          = simple_rmdir,
        .mknod          = p4fs_mknod,
        .rename         = simple_rename,
};



/*
 * FIXME: Fix my comment. Also my functionality
 * Create a directory which can be used to hold files.  This code is
 * almost identical to the "create file" logic, except that we create
 * the inode with a different mode, and use the libfs "simple" operations.
 */
static struct dentry *p4fs_create_dir (struct super_block *sb,
                struct dentry *parent, const char *name)
{
        struct dentry *dentry;
        struct inode *inode;
        struct qstr qname;

        qname.name = name;
        qname.len = strlen (name);
        qname.hash = full_name_hash(name, qname.len);
        
	/* Create dentry for the directory */
	dentry = d_alloc(parent, &qname);
        if (! dentry)
                goto out;
	
	/* Create */
        inode = p4fs_get_inode(sb, parent->d_inode, S_IFDIR | 0644);
        if (! inode)
                goto out_dput;

        d_add(dentry, inode);
        return dentry;

        out_dput:
                dput(dentry);
        out:
                return 0;
}




/*
 * FIXME: FIx my doccumentation
 * Create a file mapping a name to a counter
 * @sb: Superblock for the file system
 * @dir: Dentry for the directory where file is to be created
 * @name: Name of the file
 * @data: Data for the file
 */
static struct dentry *p4fs_create_file (struct super_block *sb,
                struct dentry *dir, const char *name,
                void *data)
{
        struct dentry *dentry;
        struct inode *inode;
        struct qstr qname;
        
        qname.name = name;
        qname.len = strlen (name);
        qname.hash = full_name_hash(name, qname.len);

        /* Create a dcache entry for the file in directory dir */ 
        dentry = d_alloc(dir, &qname);
        if (! dentry)
                goto out;
	/* Create an inode*/
        inode = p4fs_get_inode(sb, dir->d_inode, S_IFREG | 0644);
        if (! inode)
                goto out_dput;
        /* set pointer to data of the file */
        inode->i_private = data;

        /* Add inode to the dentry cache */
        d_add(dentry, inode);
        return dentry;

        out_dput:
                dput(dentry);
        out:
                return 0;
}




static char data[PROC_INFO_SIZE];
static char sig_file_data[BUFSIZE];

/*
 * FIXME: Add documentation
 */
static void p4fs_create_proc_hierarchy(struct super_block *sb, 
		struct dentry *root, struct task_struct *task)
{
	struct task_struct *child;
	struct dentry *dir;
	char dir_name[20];
	char file_name[20];
	char *signal = "signal";
	long process_state;
	char *process_state_name;

	struct mm_struct *task_mm_struct;	

	snprintf(dir_name, 20, "%d", task->pid);
	snprintf(file_name, 20, "%d.status", task->pid);

	/* Get process data */
	process_state = task->state;
	task_mm_struct = task->mm;
	if (!task_mm_struct) {
		printk("PID: %d - mm is NULL\n", task->pid);
		task_mm_struct = task->active_mm;
		if (!task_mm_struct) {
			printk("PID: %d - active_mm is also NULL\n", task->pid);
		}
	}

	switch (process_state) {
	case 0:
		process_state_name = "TASK_RUNNING";
		break;
	case 1:
		process_state_name = "TASK_INTERRUPTIBLE";
		break;
	case 2:
		process_state_name = "TASK_UNINTERRUPTIBLE";
		break;
	case 4:
		process_state_name = "TASK_STOPPED";
		break;
	case 8:
		process_state_name = "TASK_TRACED";
		break;
	default:
		process_state_name = "";
		break;
	}

	/*Create data here that you want to write to the file.*/
//	snprintf(data, BUFSIZE, "Process: %s\nProcess State: %s\nProcess Priority: %d\nTask Size: %li\nCode Start: %lx, Code End: %lx\n Heap Start: %lx, Heap End: %lx\n", 
//			task->comm, process_state_name, task->prio, task->mm->task_size, task->mm->start_code, task->mm->end_code, task->mm->start_brk, task->mm->brk);

	snprintf(data, BUFSIZE, "Process: %s\nProcess State: %s\nProcess Priority: %d static priority: %d normal priority: %d\nStart Time: %llu",
                        task->comm, process_state_name, task->prio, task->static_prio, task->normal_prio, task->start_time);

	memset(sig_file_data, 0, BUFSIZE);
	
	dir = p4fs_create_dir(sb, root, dir_name);
	if (dir)
	{
		p4fs_create_file(sb, dir, file_name, data);
		p4fs_create_file(sb, dir, signal, sig_file_data);
	}
	/* Now for all the children of this process */
	list_for_each_entry(child, &task->children, sibling){
		p4fs_create_proc_hierarchy(sb, dir, child);
	} 
}


/*
 * FIXME: Write doccunentation
 */
static int p4fs_fill_super(struct super_block *sb, void *data, int silent) {
	struct inode *root;
	
	/*
	 * Setting parameters in superblock 
	 */
	sb->s_maxbytes = MAX_LFS_FILESIZE;
	sb->s_blocksize = PAGE_CACHE_SIZE;
	sb->s_blocksize_bits = PAGE_CACHE_SHIFT;
	sb->s_magic = P4FS_MAGIC;
	sb->s_op = &p4fs_super_operations;

	/* Create an inode for the root directory of the filesystem */
	root = p4fs_get_inode(sb, NULL, S_IFDIR | 0755);
	if (!root) 
		return -ENOMEM;	
	/* Create a dentry for pfs root */
 	sb->s_root = d_make_root(root);
        if (!sb->s_root)
                return -ENOMEM;

	/* FIXME: Appropriate place to create files on this filesystem? */
	//p4fs_create_files(sb, sb->s_root);
	p4fs_create_proc_hierarchy(sb, sb->s_root, &init_task);
	return 0;
}


/* FIXME: Parameter Description
 * Will be called by vfs_mount to mount this file system 
 * @fs_type: Filesystem type structure
 * @flags: 
 * @dev_name: 
 * @data: 
 */
struct dentry *p4fs_mount(struct file_system_type *fs_type,
        int flags, const char *dev_name, void *data)
{
        return mount_nodev(fs_type, flags, data, p4fs_fill_super);
}


/* file_system_type struct for p4fs */
static struct file_system_type p4fs_fs_type = {
	.name		= "p4fs",
	.owner		= THIS_MODULE,	
	.mount		= p4fs_mount,
	.kill_sb 	= kill_litter_super,
//	.fs_flags       = FS_USERNS_MOUNT,
};


/* Module initialization. Register the filesystem */
static int __init p4fs_init(void) {
	return register_filesystem(&p4fs_fs_type);
}

/* Unregister the filesustem at module exit */
static void __exit p4fs_exit(void)
{
        unregister_filesystem(&p4fs_fs_type);
}


module_init(p4fs_init);
module_exit(p4fs_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Talha Ghaffar <gtalha@vt.edu>");
MODULE_DESCRIPTION("Process filesystem for Linux");
