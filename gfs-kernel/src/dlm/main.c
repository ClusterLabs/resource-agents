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
#define MAX_DEBUG_MSG_LEN       (64)
#else
#define LOCK_DLM_DEBUG_SIZE     (0)
#define MAX_DEBUG_MSG_LEN       (0)
#endif

static char *                   debug_buf;
static unsigned int             debug_size;
static unsigned int             debug_point;
static int                      debug_wrap;
static spinlock_t               debug_lock;
static struct proc_dir_entry *  debug_proc_entry = NULL;


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
int lock_dlm_debug_info(char *b, char **start, off_t offset, int length)
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

#ifdef CONFIG_PROC_FS
	debug_proc_entry = create_proc_entry("cluster/lock_dlm_debug", S_IRUGO,
					     NULL);
	if (debug_proc_entry)
		debug_proc_entry->get_info = &lock_dlm_debug_info;
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
	if (debug_proc_entry)
		remove_proc_entry("cluster/lock_dlm_debug", NULL);
#endif
	debug_setup(0);
}

module_init(init_lock_dlm);
module_exit(exit_lock_dlm);

MODULE_DESCRIPTION("GFS DLM Locking Module");
MODULE_AUTHOR("Red Hat, Inc.");
MODULE_LICENSE("GPL");
