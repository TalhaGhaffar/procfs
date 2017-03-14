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

#define P4FS_MAGIC 0x18926734



/* Super block operations for p4fs superblock object */
static struct super_operations p4fs_s_ops = {
	.statfs		= simple_statfs,
	.drop_inode 	= generic_delete_inode,
};


static struct inode_operations p4fs_file_inode_operations = {
        .setattr        = simple_setattr,
        .getattr        = simple_getattr,
};


/*
 * Open a file.  All we have to do here is to copy over a
 * copy of the counter pointer so it's easier to get at.
 */
static int p4fs_open(struct inode *inode, struct file *filp)
{
        filp->private_data = inode->i_private;
        return 0;
}

#define TMPSIZE 20
#define BUFSIZE 500

/*
 * Read a file.  Here we increment and read the counter, then pass it
 * back to the caller.  The increment only happens if the read is done
 * at the beginning of the file (offset = 0); otherwise we end up counting
 * by twos.
 */
static ssize_t p4fs_read_file(struct file *filp, char *buf,
                size_t count, loff_t *offset)
{
        atomic_t *counter = (atomic_t *) filp->private_data;
        int v, len;
        char tmp[TMPSIZE];
/*
 * Encode the value, and figure out how much of it we can pass back.
 */
        v = atomic_read(counter);
        if (*offset > 0) 
                v -= 1;  /* the value returned when offset was zero */
        else
                atomic_inc(counter); 
        len = snprintf(tmp, TMPSIZE, "%d\n", v);
        if (*offset > len)
                return 0; 
        if (count > len - *offset)
                count = len - *offset;
/*
 * Copy it back, increment the offset, and we're done.
 */
        if (copy_to_user(buf, tmp + *offset, count))
                return -EFAULT;
        *offset += count;
        return count;
}

/*
 * FIXME: 
 * 	From where am I called?
 *	Read data from the file
 *	Use file's name to get pid, use pid to get information from process' task_struct
 *	Copy the updated data to the user space.
 */
static ssize_t p4fs_read_file_(struct file *file_p, char *buf, 
		size_t count, loff_t *offset)
{
	/*  */
	
}


/*
 * Write a file.
 */
static ssize_t p4fs_write_file(struct file *filp, const char *buf,
                size_t count, loff_t *offset)
{
        atomic_t *counter = (atomic_t *) filp->private_data;
        char tmp[TMPSIZE];
/*
 * Only write from the beginning.
 */
        if (*offset != 0)
                return -EINVAL;
/*
 * Read the value from the user.
 */
        if (count >= TMPSIZE)
                return -EINVAL;
        memset(tmp, 0, TMPSIZE);
        if (copy_from_user(tmp, buf, count))
                return -EFAULT;
/*
 * Store it in the counter and we are done.
 */
        atomic_set(counter, simple_strtol(tmp, NULL, 10));
        return count;
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
static struct inode *p4fs_get_inode(struct super_block *sb, 
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
                        n_inode->i_op = &simple_dir_inode_operations;	/*FIXME: Write your own operations ? */
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
        dentry = d_alloc(parent, &qname);
        if (! dentry)
                goto out;

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
 * FIXME: FIx my doccumentation you dumb fuck
 * Create a file mapping a name to a counter.
 */
static struct dentry *p4fs_create_file (struct super_block *sb,
                struct dentry *dir, const char *name,
                void *data)
{
	/* FIXME: I don't want to deal with these dumb counters. Do something useful you shit */
        struct dentry *dentry;
        struct inode *inode;
        struct qstr qname;
	/*
 	 * Make a hashed version of the name to go with the dentry.
 	 */
        qname.name = name;
        qname.len = strlen (name);
        qname.hash = full_name_hash(name, qname.len);
	/*
 	 * Now we can create our dentry and the inode to go with it.
 	 */
        dentry = d_alloc(dir, &qname);
        if (! dentry)
                goto out;
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

static struct dentry *p4fs_init_process_file(struct super_block *sb, 
			struct dentry *dir, const char *name) 
{
	struct dentry *dentry;
	struct qstr qname;
	struct inode *inode;
	char data[100];
	struct task_struct *init_proc = &init_task;
	
	snprintf(data, 100, "name=%s, pid=%d, state=%li\n", init_proc->comm, init_proc->pid, init_proc->state);	

	qname.name = name;
	qname.len = strlen(name);
	qname.hash = full_name_hash(name, qname.len);

	dentry = d_alloc(dir, &qname);
	if (!dentry)
		return 0;
	
	inode = p4fs_get_inode(sb, dir->d_inode, S_IFREG | 0644);
	if (!inode)
		goto out_dput;

	inode->i_private = data;
	
	d_add(dentry, inode);
	return dentry;

	out_dput:
		dput(dentry);	
	
	return 0;
}
	


/*
 * Create files on the root directory
 */
static atomic_t counter, subcounter;

static void p4fs_create_files (struct super_block *sb, struct dentry *root)
{
	/*FIXME: Change me. I am useless right now */
        struct dentry *subdir;
	/*
	 * One counter in the top-level directory.
	 */
        atomic_set(&counter, 0);
        p4fs_create_file(sb, root, "counter", &counter);
	/*
	 * And one in a subdirectory.
	 */
        atomic_set(&subcounter, 0);
        subdir = p4fs_create_dir(sb, root, "subdir");
        if (subdir)
                p4fs_create_file(sb, subdir, "subcounter", &subcounter);

	p4fs_init_process_file(sb, root, "init");
}


static void p4fs_create_proc_hierarchy(struct super_block *sb, 
		struct dentry *root, struct task_struct *task)
{
	struct task_struct *child;
	struct dentry *dir;
	char dir_name[20];
	char file_name[20];
	char *signal = "signal";
	char data[500];
	
	snprintf(dir_name, sizeof(pid_t), "%d", task->pid);
	snprintf(file_name, 20, "%d.status", task->pid);
	/*Create data here that you want to write to the file.*/
	snprintf(data, 500, "Process: %s\nProcess State: %li\n", task->comm, task->state);
	/*Create a directory for init process: name = pid*/
	/*Create files for the process, 1: pid.status, 2: signal*/
	dir = p4fs_create_dir(sb, root, dir_name);
	if (dir)
	{
		p4fs_create_file(sb, dir, file_name, data);
		p4fs_create_file(sb, dir, signal, 0);
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
	sb->s_blocksize = PAGE_CACHE_SIZE;
	sb->s_blocksize_bits = PAGE_CACHE_SHIFT;
	sb->s_magic = P4FS_MAGIC;
	sb->s_op = &p4fs_s_ops;

	/* Create an iinode for the root directory of the filesystem */
	root = p4fs_get_inode(sb, NULL, S_IFDIR | 0755);
	if (!root) 
		return -ENOMEM;	
	/* Create a dentry for pfs root */
 	sb->s_root = d_make_root(root);
        if (!sb->s_root)
                return -ENOMEM;

	/* FIXME: Appropriate place to create files on this filesystem? */
	p4fs_create_files(sb, sb->s_root);
	p4fs_create_proc_hierarchy(sb, sb->s_root, &init_task);
	return 0;
}


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
	.fs_flags       = FS_USERNS_MOUNT,
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
