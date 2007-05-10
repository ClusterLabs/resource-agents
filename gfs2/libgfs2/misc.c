/******************************************************************************
*******************************************************************************
**
**  Copyright (C) Sistina Software, Inc.  1997-2003  All rights reserved.
**  Copyright (C) 2005-2007 Red Hat, Inc.  All rights reserved.
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
#include <errno.h>
#include <sys/mount.h>
#include <linux/types.h>
#include <sys/file.h>

#include "libgfs2.h"

void
compute_constants(struct gfs2_sbd *sdp)
{
	uint32_t hash_blocks, ind_blocks, leaf_blocks;
	uint32_t tmp_blocks;
	unsigned int x;

	sdp->md.next_inum = 1;

	sdp->bsize_shift = ffs(sdp->bsize) - 1;
	sdp->sb_addr = GFS2_SB_ADDR * GFS2_BASIC_BLOCK / sdp->bsize;

	sdp->sd_fsb2bb_shift = sdp->bsize_shift -
		GFS2_BASIC_BLOCK_SHIFT;
	sdp->sd_fsb2bb = 1 << sdp->sd_fsb2bb_shift;
	sdp->sd_diptrs = (sdp->bsize - sizeof(struct gfs2_dinode)) /
		sizeof(uint64_t);
	sdp->sd_inptrs = (sdp->bsize - sizeof(struct gfs2_meta_header)) /
		sizeof(uint64_t);
	sdp->sd_jbsize = sdp->bsize - sizeof(struct gfs2_meta_header);
	sdp->sd_hash_bsize = sdp->bsize / 2;
	sdp->sd_hash_bsize_shift = sdp->bsize_shift - 1;
	sdp->sd_hash_ptrs = sdp->sd_hash_bsize / sizeof(uint64_t);

	/*  Compute maximum reservation required to add a entry to a directory  */

	hash_blocks = DIV_RU(sizeof(uint64_t) * (1 << GFS2_DIR_MAX_DEPTH),
			     sdp->sd_jbsize);

	ind_blocks = 0;
	for (tmp_blocks = hash_blocks; tmp_blocks > sdp->sd_diptrs;) {
		tmp_blocks = DIV_RU(tmp_blocks, sdp->sd_inptrs);
		ind_blocks += tmp_blocks;
	}

	leaf_blocks = 2 + GFS2_DIR_MAX_DEPTH;

	sdp->sd_max_dirres = hash_blocks + ind_blocks + leaf_blocks;

	sdp->sd_heightsize[0] = sdp->bsize - sizeof(struct gfs2_dinode);
	sdp->sd_heightsize[1] = sdp->bsize * sdp->sd_diptrs;
	for (x = 2;; x++) {
		uint64_t space, d;
		uint32_t m;

		space = sdp->sd_heightsize[x - 1] * sdp->sd_inptrs;
		d = space;
		m = do_div(d, sdp->sd_inptrs);

		if (d != sdp->sd_heightsize[x - 1] || m)
			break;
		sdp->sd_heightsize[x] = space;
	}
	sdp->sd_max_height = x;
	if (sdp->sd_max_height > GFS2_MAX_META_HEIGHT)
		die("bad constants (1)\n");

	sdp->sd_jheightsize[0] = sdp->bsize - sizeof(struct gfs2_dinode);
	sdp->sd_jheightsize[1] = sdp->sd_jbsize * sdp->sd_diptrs;
	for (x = 2;; x++) {
		uint64_t space, d;
		uint32_t m;

		space = sdp->sd_jheightsize[x - 1] * sdp->sd_inptrs;
		d = space;
		m = do_div(d, sdp->sd_inptrs);

		if (d != sdp->sd_jheightsize[x - 1] || m)
			break;
		sdp->sd_jheightsize[x] = space;
	}
	sdp->sd_max_jheight = x;
	if (sdp->sd_max_jheight > GFS2_MAX_META_HEIGHT)
		die("bad constants (2)\n");
}

int 
find_gfs2_meta(struct gfs2_sbd *sdp)
{
	FILE *fp = fopen("/proc/mounts", "r");
	char name[] = "gfs2meta";
	char buffer[PATH_MAX];
	char fstype[80], mfsoptions[PATH_MAX];
	char meta_device[PATH_MAX];
	char meta_path[PATH_MAX];
	int fsdump, fspass;

	if (fp == NULL) {
		perror("open: /proc/mounts");
		exit(EXIT_FAILURE);
	}
	sdp->metafs_mounted = FALSE;
	memset(sdp->metafs_path, 0, sizeof(sdp->metafs_path));
	memset(meta_path, 0, sizeof(meta_path));
	while ((fgets(buffer, PATH_MAX - 1, fp)) != NULL) {
		buffer[PATH_MAX - 1] = '\0';
		if (strstr(buffer, name) == 0)
			continue;

		if (sscanf(buffer, "%s %s %s %s %d %d", meta_device, 
			   meta_path, fstype,mfsoptions, &fsdump, 
			   &fspass) != 6)
			continue;
		
		if (strcmp(meta_device, sdp->device_name) == 0 ||
		    strcmp(meta_device, sdp->path_name) == 0) {
			fclose(fp);
			sdp->metafs_mounted = FALSE;
			strcpy(sdp->metafs_path, meta_path);
			return TRUE;
		}
	}
	fclose(fp);
	return FALSE;
}

int
dir_exists(const char *dir)
{
	int fd, ret;
	struct stat statbuf;
	fd = open(dir, O_RDONLY);
	if (fd<0) { 
		if (errno == ENOENT)
			return 0;
		die("Couldn't open %s : %s\n", dir, strerror(errno));
	}
	ret = fstat(fd, &statbuf);
	if (ret)
		die("stat failed on %s : %s\n", dir, strerror(errno));
	if (S_ISDIR(statbuf.st_mode)) {
		close(fd);
		return 1;
	}
	close(fd);
	die("%s exists, but is not a directory. Cannot mount metafs here\n", dir);
}

void
check_for_gfs2(struct gfs2_sbd *sdp)
{
	FILE *fp = fopen("/proc/mounts", "r");
	char *name = sdp->path_name;
	char buffer[PATH_MAX];
	char fstype[80];
	int fsdump, fspass, ret;
	char fspath[PATH_MAX];
	char fsoptions[PATH_MAX];

	if (name[strlen(name) - 1] == '/')
		name[strlen(name) - 1] = '\0';

	if (fp == NULL) {
		perror("open: /proc/mounts");
		exit(EXIT_FAILURE);
	}
	while ((fgets(buffer, PATH_MAX - 1, fp)) != NULL) {
		buffer[PATH_MAX - 1] = 0;

		if (strstr(buffer, "0") == 0)
			continue;

		if ((ret = sscanf(buffer, "%s %s %s %s %d %d",
				  sdp->device_name, fspath, 
				  fstype, fsoptions, &fsdump, &fspass)) != 6) 
			continue;

		if (strcmp(fstype, "gfs2") != 0)
			continue;

		/* Check if they specified the device instead of mnt point */
		if (strcmp(sdp->device_name, name) == 0)
			strcpy(sdp->path_name, fspath); /* fix it */
		else if (strcmp(fspath, name) != 0)
			continue;

		fclose(fp);
		if (strncmp(sdp->device_name, "/dev/loop", 9) == 0)
			die("Cannot perform this operation on a loopback GFS2 mount.\n");

		return;
	}
	fclose(fp);
	die("gfs2 Filesystem %s is not mounted.\n", name);
}

void 
mount_gfs2_meta(struct gfs2_sbd *sdp)
{
	int ret;
	/* mount the meta fs */
	strcpy(sdp->metafs_path, "/tmp/.gfs2meta");
	if (!dir_exists(sdp->metafs_path)) {
		ret = mkdir(sdp->metafs_path, 0700);
		if (ret)
			die("Couldn't create %s : %s\n", sdp->metafs_path,
			    strerror(errno));
	}
		
	ret = mount(sdp->device_name, sdp->metafs_path, "gfs2meta", 0, NULL);
	if (ret)
		die("Couldn't mount %s : %s\n", sdp->metafs_path,
		    strerror(errno));
}

void
lock_for_admin(struct gfs2_sbd *sdp)
{
	int error;

	if (sdp->debug)
		printf("\nTrying to get admin lock...\n");

	sdp->metafs_fd = open(sdp->metafs_path, O_RDONLY | O_NOFOLLOW);
	if (sdp->metafs_fd < 0)
		die("can't open %s: %s\n",
		    sdp->metafs_path, strerror(errno));
	
	error = flock(sdp->metafs_fd, LOCK_EX);
	if (error)
		die("can't flock %s: %s\n", sdp->metafs_path, strerror(errno));
	if (sdp->debug)
		printf("Got it.\n");
}

void
cleanup_metafs(struct gfs2_sbd *sdp)
{
	int ret;

	if (sdp->metafs_fd <= 0)
		return;

	fsync(sdp->metafs_fd);
	close(sdp->metafs_fd);
	if (!sdp->metafs_mounted) { /* was mounted by us */
		ret = umount(sdp->metafs_path);
		if (ret)
			fprintf(stderr, "Couldn't unmount %s : %s\n",
				sdp->metafs_path, strerror(errno));
	}
}

