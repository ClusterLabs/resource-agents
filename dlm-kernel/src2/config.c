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

#include <linux/module.h>
#include <linux/proc_fs.h>

#include "dlm_internal.h"
#include "lowcomms.h"
#include "config.h"

/* Config file defaults */
#define DEFAULT_TCP_PORT       21064
#define DEFAULT_LOCK_TIMEOUT      30
#define DEFAULT_BUFFER_SIZE     4096
#define DEFAULT_RSBTBL_SIZE      256
#define DEFAULT_LKBTBL_SIZE     1024
#define DEFAULT_DIRTBL_SIZE      512
#define DEFAULT_CONN_INCREMENT    32
#define DEFAULT_DEADLOCKTIME      10
#define DEFAULT_RECOVER_TIMER      5

struct config_info dlm_config = {
	.tcp_port = DEFAULT_TCP_PORT,
	.lock_timeout = DEFAULT_LOCK_TIMEOUT,
	.buffer_size = DEFAULT_BUFFER_SIZE,
	.rsbtbl_size = DEFAULT_RSBTBL_SIZE,
	.lkbtbl_size = DEFAULT_LKBTBL_SIZE,
	.dirtbl_size = DEFAULT_DIRTBL_SIZE,
	.conn_increment = DEFAULT_CONN_INCREMENT,
	.deadlocktime = DEFAULT_DEADLOCKTIME,
	.recover_timer = DEFAULT_RECOVER_TIMER
};


static struct config_proc_info {
    char *name;
    int  *value;
} config_proc[] = {
    {
	.name = "tcp_port",
	.value = &dlm_config.tcp_port,
    },
    {
	.name = "lock_timeout",
	.value = &dlm_config.lock_timeout,
    },
    {
	.name = "buffer_size",
	.value = &dlm_config.buffer_size,
    },
    {
	.name = "rsbtbl_size",
	.value = &dlm_config.rsbtbl_size,
    },
    {
	.name = "lkbtbl_size",
	.value = &dlm_config.lkbtbl_size,
    },
    {
	.name = "dirtbl_size",
	.value = &dlm_config.dirtbl_size,
    },
    {
	.name = "conn_increment",
	.value = &dlm_config.conn_increment,
    },
    {
	.name = "deadlocktime",
	.value = &dlm_config.deadlocktime,
    },
    {
	.name = "recover_timer",
	.value = &dlm_config.recover_timer,
    }
};
static struct proc_dir_entry *dlm_dir;

static int dlm_config_read_proc(char *page, char **start, off_t off, int count,
				int *eof, void *data)
{
	struct config_proc_info *cinfo = data;
	return snprintf(page, count, "%d\n", *cinfo->value);
}

static int dlm_config_write_proc(struct file *file, const char *buffer,
				 unsigned long count, void *data)
{
	struct config_proc_info *cinfo = data;
	int value;
	char *end;

	value = simple_strtoul(buffer, &end, 10);
	if (*end)
		*cinfo->value = value;
	return count;
}

int dlm_config_init(void)
{
	int i;
	struct proc_dir_entry *pde;

	if (!proc_mkdir("cluster/config", 0))
		return -1;

	dlm_dir = proc_mkdir("cluster/config/dlm", 0);
	if (!dlm_dir) {
		remove_proc_entry("cluster/config", NULL);
		return -1;
	}

	dlm_dir->owner = THIS_MODULE;

	for (i=0; i<sizeof(config_proc)/sizeof(struct config_proc_info); i++) {
		pde = create_proc_entry(config_proc[i].name, 0660, dlm_dir);
		if (pde) {
			pde->data = &config_proc[i];
			pde->write_proc = dlm_config_write_proc;
			pde->read_proc = dlm_config_read_proc;
		}
	}
	return 0;
}

void dlm_config_exit(void)
{
	int i;

	for (i=0; i<sizeof(config_proc)/sizeof(struct config_proc_info); i++)
		remove_proc_entry(config_proc[i].name, dlm_dir);
	remove_proc_entry("cluster/config/dlm", NULL);
	remove_proc_entry("cluster/config", NULL);
}
