/******************************************************************************
*******************************************************************************
**
**  Copyright (C) 2008 Red Hat, Inc.  All rights reserved.
**
**  This copyrighted material is made available to anyone wishing to use,
**  modify, copy, or redistribute it subject to the terms and conditions
**  of the GNU General Public License v.2.
**
*******************************************************************************
******************************************************************************/

#include "gfs_daemon.h"
#include "libfenced.h"

int we_are_in_fence_domain(void)
{
	struct fenced_node nodeinfo;
	int rv;

	memset(&nodeinfo, 0, sizeof(nodeinfo));

	rv = fenced_node_info(our_nodeid, &nodeinfo);
	if (rv < 0) {
		log_debug("fenced_node_info error %d", rv);
		return 0;
	}

	if (nodeinfo.member)
		return 1;
	return 0;
}

#define SYSFS_DIR       "/sys/fs"
#define SYSFS_BUFLEN    64

int set_sysfs(struct mountgroup *mg, char *field, int val)
{
	char fname[PATH_MAX];
	char out[SYSFS_BUFLEN];
	int rv, fd;

	snprintf(fname, PATH_MAX, "%s/%s/%s/lock_module/%s",
		 SYSFS_DIR, mg->mount_args.type, mg->mount_args.table, field);

	log_group(mg, "set %s to %d", fname, val);

	fd = open(fname, O_RDWR);
	if (fd < 0) {
		log_group(mg, "set open %s error %d %d", fname, fd, errno);
		return -1;
	}

	mg->got_kernel_mount = 1;

	memset(out, 0, sizeof(out));
	sprintf(out, "%d", val);

	rv = write(fd, out, strlen(out));

	close(fd);
	return 0;
}

static int get_sysfs(struct mountgroup *mg, char *field, char *buf, int len)
{
	char fname[PATH_MAX], *p;
	int fd, rv;

	snprintf(fname, PATH_MAX, "%s/%s/%s/lock_module/%s",
		 SYSFS_DIR, mg->mount_args.type, mg->mount_args.table, field);

	fd = open(fname, O_RDONLY);
	if (fd < 0) {
		log_group(mg, "get open %s error %d %d", fname, fd, errno);
		return -1;
	}

	mg->got_kernel_mount = 1;

	rv = read(fd, buf, len);
	if (rv < 0)
		log_error("read %s error %d %d", fname, rv, errno);
	else {
		rv = 0;
		p = strchr(buf, '\n');
		if (p)
			*p = '\0';
	}

	close(fd);
	return rv;
}

int read_sysfs_int(struct mountgroup *mg, char *field, int *val_out)
{
	char buf[SYSFS_BUFLEN];
	int rv;

	memset(buf, 0, sizeof(buf));

	rv = get_sysfs(mg, field, buf, sizeof(buf));
	if (rv < 0)
		return rv;

	*val_out = atoi(buf);
	return 0;
}

int run_dmsetup_suspend(struct mountgroup *mg, char *dev)
{
	struct sched_param sched_param;
	char buf[PATH_MAX];
	pid_t pid;
	int i, rv;

	memset(buf, 0, sizeof(buf));
	rv = readlink(dev, buf, PATH_MAX);
	if (rv < 0)
		strncpy(buf, dev, sizeof(buf));

	log_group(mg, "run_dmsetup_suspend %s (orig %s)", buf, dev);

	pid = fork();
	if (pid < 0)
		return -1;

	if (pid) {
		mg->dmsetup_wait = 1;
		mg->dmsetup_pid = pid;
		return 0;
	} else {
		sched_param.sched_priority = 0;
		sched_setscheduler(0, SCHED_OTHER, &sched_param);

		for (i = 0; i < 50; i++)
			close(i);

		execlp("dmsetup", "dmsetup", "suspend", buf, NULL);
		exit(EXIT_FAILURE);
	}
	return -1;
}

static void dmsetup_suspend_done(struct mountgroup *mg, int rv)
{
	log_group(mg, "dmsetup_suspend_done result %d", rv);
	mg->dmsetup_wait = 0;
	mg->dmsetup_pid = 0;

	if (!rv) {
		mg->withdraw = 1;
		if (mg->old_group_mode)
			send_withdraw_old(mg);
	}
}

void update_dmsetup_wait(void)
{
	struct mountgroup *mg;
	int status;
	int waiting = 0;
	pid_t pid;

	list_for_each_entry(mg, &mountgroups, list) {
		if (mg->dmsetup_wait) {
			pid = waitpid(mg->dmsetup_pid, &status, WNOHANG);

			/* process not exited yet */
			if (!pid) {
				waiting++;
				continue;
			}

			if (pid < 0) {
				log_error("update_dmsetup_wait %s: waitpid %d "
					  "error %d", mg->name,
					  mg->dmsetup_pid, errno);
				dmsetup_suspend_done(mg, -2);
				continue;
			}

			/* process exited */

			if (!WIFEXITED(status) || WEXITSTATUS(status))
				dmsetup_suspend_done(mg, -1);
			else
				dmsetup_suspend_done(mg, 0);
		}
	}

	if (!waiting) {
		dmsetup_wait = 0;
		log_debug("dmsetup_wait off");
	}
}

