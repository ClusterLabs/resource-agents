/******************************************************************************
*******************************************************************************
**
**  Copyright (C) Sistina Software, Inc.  1997-2003  All rights reserved.
**  Copyright (C) 2005 Red Hat, Inc.  All rights reserved.
**
**  This copyrighted material is made available to anyone wishing to use,
**  modify, copy, or redistribute it subject to the terms and conditions
**  of the GNU General Public License v.2.
**
*******************************************************************************
******************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <inttypes.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>
#include <sys/ioctl.h>
#include <sys/file.h>
#include <sys/param.h>
#include <errno.h>

#define __user
#include <linux/gfs2_ioctl.h>
#include <linux/gfs2_ondisk.h>

#include "gfs2_mkfs.h"

/**
 * check_for_gfs2 - Check to see if a descriptor is a file on a GFS2 filesystem
 * @fd: the file descriptor
 * @path: the path used to open the descriptor
 *
 */

void
check_for_gfs2(struct gfs2_sbd *sdp)
{
	unsigned int magic = 0;
	int error;

	error = ioctl(sdp->path_fd, GFS2_IOCTL_IDENTIFY, &magic);
	if (error || magic != GFS2_MAGIC)
		die("%s is not a GFS2 file/filesystem\n",
		    sdp->path_name);
}

void
lock_for_admin(struct gfs2_sbd *sdp)
{
	char name[PATH_MAX];
	int fd;
	struct gfs2_ioctl gi;
	struct gfs2_dinode di;
	int error;

	if (sdp->debug)
		printf("\nTrying to get admin lock...\n");

	error = snprintf(name, PATH_MAX, "%s/.gfs2_admin", sdp->path_name);
	if (error >= PATH_MAX)
		die("lock_for_admin (1)\n");

	for (;;) {
		error = mkdir(name, 0700);
		if (error && errno != EEXIST)
			die("can't create %s (%d): %s\n",
			    name, error, strerror(errno));

		fd = open(name, O_RDONLY | O_NOFOLLOW);
		if (fd < 0)
			die("can't open %s: %s\n",
			    name, strerror(errno));

		error = flock(fd, LOCK_EX);
		if (error)
			die("can't flock: %s\n",
			    strerror(errno));

		{
			char *argv[] = { "get_file_stat" };

			gi.gi_argc = 1;
			gi.gi_argv = argv;
			gi.gi_data = (char *)&di;
			gi.gi_size = sizeof(struct gfs2_dinode);

			error = ioctl(fd, GFS2_IOCTL_SUPER, &gi);
			if (error != sizeof(struct gfs2_dinode))
				die("error doing do_statfs_sync (%d): %s\n",
				    error, strerror(errno));
		}

		if (di.di_uid || di.di_gid) {
			error = fchown(fd, 0, 0);
			if (error)
				die("can't chown %s: %s\n",
				    name, strerror(errno));
			close(fd);
			continue;
		}

		if ((di.di_mode & 07777) != 0700) {
			error = fchmod(fd, 0700);
			if (error)
				die("can't chmod %s: %s\n",
				    name, strerror(errno));
			close(fd);
			continue;
		}

		if (!S_ISDIR(di.di_mode) || !di.di_entries) {
			close(fd);
			continue;
		}

		break;
	}

	if (sdp->debug)
		printf("Got it.\n");
}

void
path2device(struct gfs2_sbd *sdp)
{
	FILE *file;
	char line[256], device[256];

	file = fopen("/proc/mounts", "r");
	if (!file)
		die("can't open /proc/mounts: %s\n",
		    strerror(errno));

	while (fgets(line, 256, file)) {
		char path[256], type[256];

		if (sscanf(line, "%s %s %s", device, path, type) != 3)
			continue;
		if (strcmp(path, sdp->path_name))
			continue;
		if (strcmp(type, "gfs2"))
			die("%s is not a GFS2 filesystem\n", sdp->path_name);

		sdp->device_name = strdup(device);

		break;
	}

	fclose(file);

	if (!sdp->device_name)
		die("%s doesn't seem to be mounted\n",
		    sdp->path_name);
}

void
find_block_size(struct gfs2_sbd *sdp)
{
	char *argv[] = { "get_super" };
	struct gfs2_ioctl gi;
	struct gfs2_sb sb;
	int error;

	gi.gi_argc = 1;
	gi.gi_argv = argv;
	gi.gi_data = (char *)&sb;
	gi.gi_size = sizeof(struct gfs2_sb);

	error = ioctl(sdp->path_fd, GFS2_IOCTL_SUPER, &gi);
	if (error != gi.gi_size)
		die("error doing get_super (%d): %s\n",
		    error, strerror(errno));

	sdp->bsize = sb.sb_bsize;
}

void
find_current_fssize(struct gfs2_sbd *sdp)
{
	struct gfs2_ioctl gi;
	char *argv[] = { "do_hfile_read",
			 "rindex" };
	char buf[sizeof(struct gfs2_rindex)];
	struct gfs2_rindex ri;
	uint64_t offset = 0;
	uint64_t fssize = 0, rgrps = 0;
	int error;

	for (;;) {
		gi.gi_argc = 2;
		gi.gi_argv = argv;
		gi.gi_data = buf;
		gi.gi_size = sizeof(struct gfs2_rindex);
		gi.gi_offset = offset;

		error = ioctl(sdp->path_fd, GFS2_IOCTL_SUPER, &gi);
		if (!error)
			break;
		if (error != sizeof(struct gfs2_rindex))
			die("error doing do_hfile_read (%d): %s\n",
			    error, strerror(errno));

		gfs2_rindex_in(&ri, buf);

		if (fssize < ri.ri_data0 + ri.ri_data)
			fssize = ri.ri_data0 + ri.ri_data;
		rgrps++;

		offset += sizeof(struct gfs2_rindex);
	}

	sdp->orig_fssize = sdp->fssize = fssize;
	sdp->orig_rgrps = sdp->rgrps = rgrps;

	if (sdp->debug) {
		printf("\nOndisk Filesystem Size:\n");
		printf("  %"PRIu64" filesystem blocks\n", fssize);
		printf("  %"PRIu64" resource groups\n", rgrps);
	}
}

static void
add_to_rindex_i(struct gfs2_sbd *sdp, char *buf, unsigned int num)
{
	char *gi_argv[] = { "resize_add_rgrps" };
	struct gfs2_ioctl gi;
	int error;

	gi.gi_argc = 1;
	gi.gi_argv = gi_argv;
	gi.gi_data = buf;
	gi.gi_size = num * sizeof(struct gfs2_rindex);

	error = ioctl(sdp->path_fd, GFS2_IOCTL_SUPER, &gi);
	if (error)
		die("error doing resize_add_rgrps (%d): %s\n",
		    error, strerror(errno));
}

#define MAX_RG_PER_IOCTL (8192)

void
add_to_rindex(struct gfs2_sbd *sdp)
{
	char buf[MAX_RG_PER_IOCTL * sizeof(struct gfs2_rindex)];
	osi_list_t *tmp, *head;
	struct rgrp_list *rl;
	struct gfs2_rindex *ri;
	unsigned int x = 0;

	for (head = &sdp->rglist, tmp = head->next;
	     tmp != head;
	     tmp = tmp->next) {
		rl = osi_list_entry(tmp, struct rgrp_list, list);
		ri = &rl->ri;

		memcpy(buf + x * sizeof(struct gfs2_rindex), ri, sizeof(struct gfs2_rindex));
		x++;

		if (x == MAX_RG_PER_IOCTL) {
			add_to_rindex_i(sdp, buf, x);
			x = 0;
		}
	}

	if (x)
		add_to_rindex_i(sdp, buf, x);
}

void
statfs_sync(struct gfs2_sbd *sdp)
{
	char *gi_argv[] = { "do_statfs_sync" };
	struct gfs2_ioctl gi;
	int error;

	gi.gi_argc = 1;
	gi.gi_argv = gi_argv;

	error = ioctl(sdp->path_fd, GFS2_IOCTL_SUPER, &gi);
	if (error)
		die("error doing do_statfs_sync (%d): %s\n",
		    error, strerror(errno));
}

void
find_current_journals(struct gfs2_sbd *sdp)
{
	char *argv[] = { "get_hfile_stat",
			 "jindex" };
	struct gfs2_ioctl gi;
	struct gfs2_dinode di;
	int error;

	gi.gi_argc = 2;
	gi.gi_argv = argv;
	gi.gi_data = (char *)&di;
	gi.gi_size = sizeof(struct gfs2_dinode);

	error = ioctl(sdp->path_fd, GFS2_IOCTL_SUPER, &gi);
	if (error != sizeof(struct gfs2_dinode))
		die("error doing do_statfs_sync (%d): %s\n",
		    error, strerror(errno));

	sdp->orig_journals = di.di_entries - 2;

	if (sdp->debug)
		printf("\nOndisk Journals: %u\n", sdp->journals);
}

int
rename2system(struct gfs2_sbd *sdp, char *new_dir, char *new_name)
{
	char *argv[] = { "rename2system",
			 new_dir, new_name };
	struct gfs2_ioctl gi;

	gi.gi_argc = 3;
	gi.gi_argv = argv;

	return ioctl(sdp->path_fd, GFS2_IOCTL_SUPER, &gi);
}

void
make_jdata(int fd, char *value)
{
	char *argv[] = { "set_file_flag",
			 value,
			 "jdata" };
	struct gfs2_ioctl gi;
	int error;

	gi.gi_argc = 3;
	gi.gi_argv = argv;

	error = ioctl(fd, GFS2_IOCTL_SUPER, &gi);
	if (error)
		die("error doing set_file_flag (%d): %s\n",
		    error, strerror(errno));
}

uint64_t
bmap(int fd, uint64_t lblock)
{
	char *argv[] = { "get_bmap" };
	struct gfs2_ioctl gi;
	uint64_t dblock = lblock;
	int error;

	gi.gi_argc = 1;
	gi.gi_argv = argv;
	gi.gi_data = (char *)&dblock;
	gi.gi_size = sizeof(uint64_t);

	error = ioctl(fd, GFS2_IOCTL_SUPER, &gi);
	if (error)
		die("error doing get_bmap (%d): %s\n",
		    error, strerror(errno));

	if (!dblock)
		die("can't bmap\n");

	return dblock;
}





