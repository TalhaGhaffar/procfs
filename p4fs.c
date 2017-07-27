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
#define PROC_INFO_SIZE 1000


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
 * Read data from the file
 */
static ssize_t p4fs_read_file(struct file *file_p, char *buf, 
		size_t count, loff_t *offset)
{
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

	if (copy_to_user(buf, tmp + *offset, count))
		return -EFAULT;
	*offset += count;
	return count;
}

/*
 * Finction to send signal to the process
 */

static void send_signal_to_process(int signal, struct task_struct *task) {
	int ret;
	struct siginfo *si = kmalloc(sizeof(struct siginfo), GFP_KERNEL);
	
	printk("Sending singal %d to process %s\n", signal, task->comm);
	si->si_signo = signal;
	ret = send_sig_info(signal, si, task);
	if (!ret) 
		printk("Signal %d delivered to process %d (%s)\n", signal, task->pid, task->comm);
	else {
		if (ret == -ESRCH) 
			printk("Process %d no longer exists\n", task->pid);
	}
	kfree(si);
}

/*
 * Write to a file on this filesystem.
 */
static ssize_t p4fs_write_file(struct file *filp, const char *buf,
                size_t count, loff_t *offset)
{
        int len;
	char tmp[TMPSIZE];
	char *data = (char *) filp->private_data;
	long process_id;
	long signal = 0;

	/*Only write the file from start */
        if (*offset != 0)
                return -EINVAL;
        if (count >= TMPSIZE)
                return -EINVAL;
        memset(tmp, 0, TMPSIZE);
        if (copy_from_user(tmp, buf, count))
		return -EFAULT;
	
	/* Write the data */
	len = snprintf(data, BUFSIZE, "%s", (char *) tmp);
	
	if ( !strcmp((&filp->f_path)->dentry->d_name.name, "signal") ) {
		/* Writing to signal file, need to send signal to the process*/
		if ( !kstrtol((&filp->f_path)->dentry->d_parent->d_name.name, 10, &process_id)) {
			struct task_struct *task = pid_task(find_vpid(process_id), PIDTYPE_PID);
			if (task) {
				if (!kstrtol(tmp, 10, &signal)){
					send_signal_to_process((int) signal, task);
				}
			}
		}
	}
        return len;
}




/*
 * file operations structure.
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
 * Create an inode in this filesystem
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
        .mkdir          = p4fs_mkdir,
        .rmdir          = simple_rmdir,
        .mknod          = p4fs_mknod,
        .rename         = simple_rename,
};



/*
 * Create a directtory
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
 * Create a file in this filesystem
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
	char *file_data = kmalloc(PROC_INFO_SIZE, GFP_KERNEL);        
	int len;

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
	len = snprintf(file_data, PROC_INFO_SIZE, "%s", (char *) data);
	inode->i_private = file_data;
	//len = snprintf(file_data, PROC_INFO_SIZE, "%s", (char *) data);

        //inode->i_private = data;

        /* Add inode to the dentry cache */
        d_add(dentry, inode);
        return dentry;

        out_dput:
                dput(dentry);
        out:
                return 0;
}




//char data[PROC_INFO_SIZE];
//atomic_t signal_data;
//char sig_file_data[BUFSIZE];

/*
 * Creates process hierarchy on the filesystem
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
	char sig_file_data[BUFSIZE];
	char data[PROC_INFO_SIZE];
	struct thread_info * thread_info;
	struct mm_struct *task_mm_struct;	
	char is_kernel_thread[5] = "No";
	char *task_mem_info;	
	pid_t pid = task->pid;

	snprintf(dir_name, 20, "%d", task->pid);
	snprintf(file_name, 20, "%d.status", task->pid);

	/* Get process data */
	process_state = task->state;
	thread_info = (struct thread_info*) task->stack;
	
	task_mm_struct = task->mm;
	task_mem_info = (char *) kmalloc (BUFSIZE, GFP_KERNEL);

	if (!task->mm) {
		snprintf(is_kernel_thread, 5, "Yes");
		memset(task_mem_info, 0 ,BUFSIZE);
	} else {
		snprintf(task_mem_info, BUFSIZE, 
				"Memory Info for the process:\n=======================\nTask Size: %0lX\nCode Start: %09lX, Code End: %09lX\n Heap Start: %09lX, Heap End: %09lX\n",
                              	task->mm->task_size, task->mm->start_code, task->mm->end_code, task->mm->start_brk, task->mm->brk);
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

	snprintf(data, PROC_INFO_SIZE, "Process: %s\nPID: %d\nProcess State: %s\nExecuting on CPU: %d\nKernel Thread: %s\nProcess Priority: %d static priority: %d normal priority: %d\nStart Time: %llu\n%s", task->comm, pid, process_state_name, thread_info->cpu, is_kernel_thread, task->prio, task->static_prio, task->normal_prio, task->start_time, task_mem_info);

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
	kfree(task_mem_info);
}


/*
 * Fill superblick for the filesystem
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

	//p4fs_create_files(sb, sb->s_root);
	p4fs_create_proc_hierarchy(sb, sb->s_root, &init_task);
	return 0;
}


/*
 * Will be called by vfs_mount to mount this file system 
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
