/******************************************************************************
*******************************************************************************
**
**  Copyright (C) Sistina Software, Inc.  1997-2003  All rights reserved.
**  Copyright (C) 2004 Red Hat, Inc.  All rights reserved.
**
**  This copyrighted material is made available to anyone wishing to use,
**  modify, copy, or redistribute it subject to the terms and conditions
**  of the GNU General Public License v.2.
**
*******************************************************************************
******************************************************************************/

#include "lock_dlm.h"
#include <linux/init.h>
#include <linux/proc_fs.h>

#if defined(LOCK_DLM_DEBUG)
#define LOCK_DLM_DEBUG_SIZE     (4096)
#define MAX_DEBUG_MSG_LEN       (80)
#else
#define LOCK_DLM_DEBUG_SIZE     (0)
#define MAX_DEBUG_MSG_LEN       (0)
#endif
#define MAX_PROC_STRING		(16)

int				lock_dlm_max_nodes;
int				lock_dlm_drop_count;
int				lock_dlm_drop_period;

static char *                   debug_buf;
static unsigned int             debug_size;
static unsigned int             debug_point;
static int                      debug_wrap;
static spinlock_t               debug_lock;
static struct proc_dir_entry *	proc_dir = NULL;
static char			proc_str[MAX_PROC_STRING + 1];


void lock_dlm_debug_log(const char *fmt, ...)
{
	va_list va;
	int i, n, size, len;
	char buf[MAX_DEBUG_MSG_LEN+1];

	spin_lock(&debug_lock);

	if (!debug_buf)
		goto out;

	size = MAX_DEBUG_MSG_LEN;
	memset(buf, 0, size+1);

	n = 0;
	/* n = snprintf(buf, size, "%s ", dlm->fsname); */
	n = snprintf(buf, size, "%u ", current->pid);
	size -= n;

	va_start(va, fmt);
	vsnprintf(buf+n, size, fmt, va);
	va_end(va);

	len = strlen(buf);
	if (len > MAX_DEBUG_MSG_LEN-1)
		len = MAX_DEBUG_MSG_LEN-1;
	buf[len] = '\n';
	buf[len+1] = '\0';

	for (i = 0; i < strlen(buf); i++) {
		debug_buf[debug_point++] = buf[i];

		if (debug_point == debug_size) {
			debug_point = 0;
			debug_wrap = 1;
		}
	}
 out:
	spin_unlock(&debug_lock);
}

static void debug_setup(int size)
{
	char *b = NULL;

	if (size > PAGE_SIZE)
		size = PAGE_SIZE;
	if (size)
		b = kmalloc(size, GFP_KERNEL);

	spin_lock(&debug_lock);
	if (debug_buf)
		kfree(debug_buf);
	if (!size || !b)
		goto out;
	debug_size = size;
	debug_point = 0;
	debug_wrap = 0;
	debug_buf = b;
	memset(debug_buf, 0, debug_size);
 out:
	spin_unlock(&debug_lock);
}

static void debug_init(void)
{
	debug_buf = NULL;
	debug_size = 0;
	debug_point = 0;
	debug_wrap = 0;
	spin_lock_init(&debug_lock);
	debug_setup(LOCK_DLM_DEBUG_SIZE);
}

void lock_dlm_debug_dump(void)
{
	int i;

	spin_lock(&debug_lock);

	if (debug_wrap) {
		for (i = debug_point; i < debug_size; i++)
			printk("%c", debug_buf[i]);
	}
	for (i = 0; i < debug_point; i++)
		printk("%c", debug_buf[i]);

	spin_unlock(&debug_lock);
}

EXPORT_SYMBOL(lock_dlm_debug_dump);

#ifdef CONFIG_PROC_FS
static int debug_info(char *b, char **start, off_t offset, int length)
{
	int i, n = 0;

	spin_lock(&debug_lock);

	if (debug_wrap) {
		for (i = debug_point; i < debug_size; i++)
			n += sprintf(b + n, "%c", debug_buf[i]);
	}
	for (i = 0; i < debug_point; i++)
		n += sprintf(b + n, "%c", debug_buf[i]);

	spin_unlock(&debug_lock);

	return n;
}

static int max_nodes_info(char *b, char **start, off_t offset, int length)
{
	return sprintf(b, "%d\n", lock_dlm_max_nodes);
}

static int drop_count_info(char *b, char **start, off_t offset, int length)
{
	return sprintf(b, "%d\n", lock_dlm_drop_count);
}

static int drop_period_info(char *b, char **start, off_t offset, int length)
{
	return sprintf(b, "%d\n", lock_dlm_drop_period);
}

static int copy_string(const char *buffer, unsigned long count)
{
	int len;

	if (count > MAX_PROC_STRING)
		len = MAX_PROC_STRING;
	else
		len = count;

	if (copy_from_user(proc_str, buffer, len))
		return -EFAULT;
	proc_str[len] = '\0';
	return len;
}

static int max_nodes_write(struct file *file, const char *buffer,
			   unsigned long count, void *data)
{
	int rv = copy_string(buffer, count);
	if (rv < 0)
		return rv;
	lock_dlm_max_nodes = (int) simple_strtol(proc_str, NULL, 0);
	return rv;
}

static int drop_count_write(struct file *file, const char *buffer,
			    unsigned long count, void *data)
{
	int rv = copy_string(buffer, count);
	if (rv < 0)
		return rv;
	lock_dlm_drop_count = (int) simple_strtol(proc_str, NULL, 0);
	return rv;
}

static int drop_period_write(struct file *file, const char *buffer,
			    unsigned long count, void *data)
{
	int rv = copy_string(buffer, count);
	if (rv < 0)
		return rv;
	lock_dlm_drop_period = (int) simple_strtol(proc_str, NULL, 0);
	return rv;
}

static void create_proc_entries(void)
{
	struct proc_dir_entry *p, *debug, *drop_count, *drop_period, *max_nodes;

	debug = drop_count = drop_period = max_nodes = NULL;

	proc_dir = proc_mkdir("cluster/lock_dlm", 0);
	if (!proc_dir)
		return;
	proc_dir->owner = THIS_MODULE;

	p = create_proc_entry("max_nodes", 0666, proc_dir);
	if (!p)
		goto out;
	p->owner = THIS_MODULE;
	p->get_info = max_nodes_info;
	p->write_proc = max_nodes_write;
	max_nodes = p;

	p = create_proc_entry("debug", 0444, proc_dir);
	if (!p)
		goto out;
	p->get_info = debug_info;
	p->owner = THIS_MODULE;
	debug = p;

	p = create_proc_entry("drop_count", 0666, proc_dir);
	if (!p)
		goto out;
	p->owner = THIS_MODULE;
	p->get_info = drop_count_info;
	p->write_proc = drop_count_write;
	drop_count = p;

	p = create_proc_entry("drop_period", 0666, proc_dir);
	if (!p)
		goto out;
	p->owner = THIS_MODULE;
	p->get_info = drop_period_info;
	p->write_proc = drop_period_write;
	drop_period = p;

	return;

 out:
	if (drop_period)
		remove_proc_entry("drop_period", proc_dir);
	if (drop_count)
		remove_proc_entry("drop_count", proc_dir);
	if (debug)
		remove_proc_entry("debug", proc_dir);
	if (max_nodes)
		remove_proc_entry("max_nodes", proc_dir);

	remove_proc_entry("cluster/lock_dlm", NULL);
	proc_dir = NULL;
}

static void remove_proc_entries(void)
{
	if (proc_dir) {
		remove_proc_entry("max_nodes", proc_dir);
		remove_proc_entry("debug", proc_dir);
		remove_proc_entry("drop_period", proc_dir);
		remove_proc_entry("drop_count", proc_dir);
		remove_proc_entry("cluster/lock_dlm", NULL);
		proc_dir = NULL;
	}
}
#endif

/**
 * init_dlm - Initialize the dlm module
 *
 * Returns: 0 on success, -EXXX on failure
 */

int __init init_lock_dlm(void)
{
	int error;

	error = lm_register_proto(&lock_dlm_ops);
	if (error) {
		printk("lock_dlm:  can't register protocol: (%d)\n", error);
		return error;
	}

	lock_dlm_max_nodes = LOCK_DLM_MAX_NODES;
	lock_dlm_drop_count = DROP_LOCKS_COUNT;
	lock_dlm_drop_period = DROP_LOCKS_PERIOD;

#ifdef CONFIG_PROC_FS
	create_proc_entries();
#endif
	debug_init();

	printk("Lock_DLM (built %s %s) installed\n", __DATE__, __TIME__);
	return 0;
}

/**
 * exit_dlm - cleanup the dlm module
 *
 */

void __exit exit_lock_dlm(void)
{
	lm_unregister_proto(&lock_dlm_ops);
#ifdef CONFIG_PROC_FS
	remove_proc_entries();
#endif
	debug_setup(0);
}

module_init(init_lock_dlm);
module_exit(exit_lock_dlm);

MODULE_DESCRIPTION("GFS DLM Locking Module");
MODULE_AUTHOR("Red Hat, Inc.");
MODULE_LICENSE("GPL");
