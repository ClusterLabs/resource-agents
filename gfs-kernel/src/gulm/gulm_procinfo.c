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

#include "gulm.h"
#include <linux/kernel.h>
#include <linux/proc_fs.h>
#include "util.h"

extern gulm_cm_t gulm_cm;

struct proc_dir_entry *gulm_proc_dir;
struct proc_dir_entry *gulm_fs_proc_dir;

/* the read operating function. */
int
gulm_fs_proc_read (char *buf, char **start, off_t off, int count, int *eof,
		   void *data)
{
	gulm_fs_t *fs = (gulm_fs_t *) data;
	count = 0;		/* ignore how much it wants */

	count += sprintf (buf + count, "Filesystem: %s\nJID: %d\n"
			  "handler_queue_cur: %d\n"
			  "handler_queue_max: %d\n",
			  fs->fs_name, fs->fsJID,
			  fs->cq.task_count, fs->cq.task_max);

	*eof = TRUE;
	if (off >= count)
		return 0;
	*start = buf + off;
	return (count - off);
}

/* read the stuff for all */
int
gulm_core_proc_read (char *buf, char **start, off_t off, int count,
		     int *eof, void *data)
{
	count = 0;		/* ignore how much it wants */

	count = sprintf (buf,
			 "cluster id: %s\n"
			 "my name: %s\n", gulm_cm.clusterID, gulm_cm.myName);

	*eof = TRUE;
	if (off >= count)
		return 0;
	*start = buf + off;
	return (count - off);
}

int
gulm_lt_proc_read (char *buf, char **start, off_t off, int count,
		   int *eof, void *data)
{
	lock_table_t *lt = (lock_table_t *) data;
	count = 0;		/* ignore how much it wants */

	count += sprintf (buf + count, "\n"
			  "lock counts:\n"
			  "  total: %d\n"
			  "    unl: %d\n"
			  "    exl: %d\n"
			  "    shd: %d\n"
			  "    dfr: %d\n"
			  "pending: %d\n"
			  "   lvbs: %d\n"
			  "   lops: %d\n\n",
			  lt->locks_total,
			  lt->locks_unl,
			  lt->locks_exl,
			  lt->locks_shd,
			  lt->locks_dfr,
			  atomic_read (&lt->locks_pending),
			  lt->locks_lvbs, lt->lops);

	*eof = TRUE;
	if (off >= count)
		return 0;
	*start = buf + off;
	return (count - off);
}

/* add entry to our proc folder
 * call this on mount.
 * */
int
add_to_proc (gulm_fs_t * fs)
{
	if (!(create_proc_read_entry (fs->fs_name, S_IFREG | S_IRUGO,
				      gulm_fs_proc_dir, gulm_fs_proc_read,
				      (void *) fs))) {
		log_err ("couldn't register proc entry for %s\n", fs->fs_name);
		return -EINVAL;
	}
	return 0;
}

/* get rid of it
 * this on umount.
 * */
void
remove_from_proc (gulm_fs_t * fs)
{
	remove_proc_entry (fs->fs_name, gulm_fs_proc_dir);
}

 /* create our own root dir.
  * initmodule
  * */
int
init_proc_dir (void)
{
	if ((gulm_proc_dir = proc_mkdir ("gulm", &proc_root)) == NULL) {
		log_err ("cannot create the gulm directory in /proc\n");
		return -EINVAL;
	}
	if (!(create_proc_read_entry ("core", S_IFREG | S_IRUGO, gulm_proc_dir,
				      gulm_core_proc_read, NULL))) {
		log_err ("couldn't register proc entry for core\n");
		remove_proc_entry ("gulm", &proc_root);
		return -EINVAL;
	}
	if ((gulm_fs_proc_dir =
	     proc_mkdir ("filesystems", gulm_proc_dir)) == NULL) {
		log_err
		    ("cannot create the filesystems directory in /proc/gulm\n");
		remove_proc_entry ("core", gulm_proc_dir);
		remove_proc_entry ("gulm", &proc_root);
		return -EINVAL;
	}
	if (!(create_proc_read_entry ("lockspace", S_IFREG | S_IRUGO,
				      gulm_proc_dir, gulm_lt_proc_read,
				      (void *) &gulm_cm.ltpx))) {
		remove_proc_entry ("filesystems", gulm_proc_dir);
		remove_proc_entry ("core", gulm_proc_dir);
		remove_proc_entry ("gulm", &proc_root);
		return -EINVAL;
	}

	return 0;
}

/* destroy it
 * close module
 * */
void
remove_proc_dir (void)
{
	remove_proc_entry ("lockspace", gulm_proc_dir);
	remove_proc_entry ("filesystems", gulm_proc_dir);
	remove_proc_entry ("core", gulm_proc_dir);
	remove_proc_entry ("gulm", &proc_root);
}
