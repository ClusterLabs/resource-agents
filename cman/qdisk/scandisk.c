/******************************************************************************
*******************************************************************************
**
**  Copyright (C) 2008 Red Hat, Inc.  All rights reserved.
**  All rights reserved.
**
**  Author: Fabio M. Di Nitto <fdinitto@redhat.com>
**
**  Original design by:
**  Joel Becker <Joel.Becker@oracle.com>
**  Fabio M. Di Nitto <fdinitto@redhat.com>
**
**  This copyrighted material is made available to anyone wishing to use,
**  modify, copy, or redistribute it subject to the terms and conditions
**  of the GNU General Public License v.2.
**
*******************************************************************************
******************************************************************************/

#include <stdio.h>
#include <time.h>
#include <stdlib.h>
#include <string.h>
#include <sys/param.h>
#include <dirent.h>
#include <sys/sysmacros.h>
#include <sys/stat.h>

#include "scandisk.h"

/** search in cache helpers **/

/*
 * match is 0 for exact match
 *          1 to see if the string is contained and return the first match
 */

static struct devnode *find_dev_by_path(struct devnode *startnode, char *path,
					int match)
{
	struct devnode *nextnode;
	struct devpath *nextpath;

	while (startnode) {
		nextnode = startnode->next;
		nextpath = startnode->devpath;
		while (nextpath) {
			if (match) {
				if (strstr(nextpath->path, path))
					return startnode;
			} else {
				if (!strcmp(nextpath->path, path))
					return startnode;
			}
			nextpath = nextpath->next;
		}
		startnode = nextnode;
	}

	return 0;
}

static struct devnode *find_dev_by_majmin(struct devnode *startnode, int maj,
					  int min)
{
	struct devnode *nextnode;

	while (startnode) {
		nextnode = startnode->next;
		if ((startnode->maj == maj) && (startnode->min == min))
			return startnode;
		startnode = nextnode;
	}

	return 0;
}

/** free the cache.. this one is easy ;) **/

/* free all the path associated to one node */
static void flush_dev_list(struct devpath *startpath)
{
	struct devpath *nextpath;

	while (startpath) {
		nextpath = startpath->next;
		free(startpath);
		startpath = nextpath;
	}

	return;
}

/* free all nodes associated with one devlist */
static void flush_dev_cache(struct devlisthead *devlisthead)
{
	struct devnode *nextnode, *startnode = devlisthead->devnode;

	while (startnode) {
		nextnode = startnode->next;
		flush_dev_list(startnode->devpath);
		free(startnode);
		startnode = nextnode;
	}

	return;
}

/** list object allocation helpers **/

/* our only certain keys in the list are maj and min
 * this function append a devnode obj to devlisthead
 * and set maj and min
 */

static struct devnode *alloc_list_obj(struct devlisthead *devlisthead, int maj,
				      int min)
{
	struct devnode *nextnode, *startnode;

	nextnode = malloc(sizeof(struct devnode));
	if (!nextnode)
		return 0;

	memset(nextnode, 0, sizeof(struct devnode));

	if (!devlisthead->devnode) {
		devlisthead->devnode = startnode = nextnode;
	} else {
		startnode = devlisthead->devnode;
		while (startnode->next)
			startnode = startnode->next;

		/* always append what we find */
		startnode->next = nextnode;
		startnode = nextnode;
	}

	startnode->maj = maj;
	startnode->min = min;

	return startnode;
}

/* really annoying but we have no way to know upfront how
 * many paths are linked to a certain maj/min combo.
 * Once we find a device, we know maj/min and this new path.
 * add_path_obj will add the given path to the devnode
 */
static int add_path_obj(struct devnode *startnode, char *path)
{
	struct devpath *nextpath, *startpath;

	nextpath = malloc(sizeof(struct devpath));
	if (!nextpath)
		return 0;

	memset(nextpath, 0, sizeof(struct devpath));

	if (!startnode->devpath) {
		startnode->devpath = startpath = nextpath;
	} else {
		startpath = startnode->devpath;
		while (startpath->next)
			startpath = startpath->next;

		/* always append what we find */
		startpath->next = nextpath;
		startpath = nextpath;
	}

	strncpy(startpath->path, path, MAXPATHLEN - 1);

	return 1;
}

/* lsdev needs to add blocks in 2 conditions: if we have a real block device
 * or if have a symlink to a block device.
 * this function simply avoid duplicate code around.
 */
static int add_lsdev_block(struct devlisthead *devlisthead, struct stat *sb,
			   char *path)
{
	int maj, min;
	struct devnode *startnode;

	maj = major(sb->st_rdev);
	min = minor(sb->st_rdev);

	startnode = find_dev_by_majmin(devlisthead->devnode, maj, min);
	if (!startnode) {
		startnode = alloc_list_obj(devlisthead, maj, min);
		if (!startnode)
			return 0;
	}

	if (!add_path_obj(startnode, path))
		return 0;

	return 1;
}

/* check if it is a device or a symlink to a device */
static int dev_is_block(struct stat *sb, char *path)
{
	if (S_ISBLK(sb->st_mode))
		return 1;

	if (S_ISLNK(sb->st_mode))
		if (!stat(path, sb))
			if (S_ISBLK(sb->st_mode))
				return 1;

	return 0;
}

/* lsdev does nothing more than ls -lR /dev
 * dives into dirs (skips hidden directories)
 * add block devices
 * parse symlinks
 *
 * ret:
 * 1 on success
 * -1 for generic errors
 * -2 -ENOMEM
 */
static int lsdev(struct devlisthead *devlisthead, char *path)
{
	int i, n, err = 0;
	struct dirent **namelist;
	struct stat sb;
	char newpath[MAXPATHLEN];

	i = scandir(path, &namelist, 0, alphasort);
	if (i < 0)
		return -1;

	for (n = 0; n < i; n++) {
		if (namelist[n]->d_name[0] != '.') {
			snprintf(newpath, sizeof(newpath), "%s/%s", path,
				 namelist[n]->d_name);

			if (!lstat(newpath, &sb)) {
				if (S_ISDIR(sb.st_mode))
					err = lsdev(devlisthead, newpath);
				if (err < 0)
					return err;

				if (dev_is_block(&sb, newpath))
					if (!add_lsdev_block
					    (devlisthead, &sb, newpath) < 0)
						return -2;
			}
		}
		free(namelist[n]);
	}
	free(namelist);
	return 1;
}

/*
 * scan /proc/partitions and adds info into the list.
 * It's able to add nodes if those are not found in sysfs.
 *
 * ret:
 *  0 if we can't scan
 *  -2 -ENOMEM
 *  1 if everything is ok
 */

static int scanprocpart(struct devlisthead *devlisthead)
{
	char line[4096];
	FILE *fp;
	int minor, major;
	unsigned long long blkcnt;
	char device[128];
	struct devnode *startnode;
	fp = fopen("/proc/partitions", "r");
	if (!fp)
		return 0;
	while (fgets(line, sizeof(line), fp)
	       != NULL) {

		if (strlen(line) > 128 + (22))
			continue;
		sscanf(line, "%4d %4d %10llu %s",
		       &major, &minor, &blkcnt, device);

		/* careful here.. if there is no device, we are scanning the
		 * first two lines that are not useful to us
		 */
		if (!strlen(device))
			continue;
		startnode =
		    find_dev_by_majmin(devlisthead->devnode, major, minor);
		if (!startnode) {
			startnode = alloc_list_obj(devlisthead, major, minor);
			if (!startnode)
				return -2;
		}

		startnode->procpart = 1;
		strcpy(startnode->procname, device);
	}

	fclose(fp);
	return 1;
}

/* scan /proc/mdstat and adds info to the list. At this point
 * all the devices _must_ be already in the list. We don't add anymore
 * since raids can only be assembled out of existing devices
 *
 * ret:
 * 1 if we could scan
 * 0 otherwise
 */
static int scanmdstat(struct devlisthead *devlisthead)
{
	char line[4096];
	FILE *fp;
	char device[16];
	char separator[4];
	char status[16];
	char personality[16];
	char firstdevice[16];
	char devices[4096];
	char *tmp, *next;
	struct devnode *startnode = NULL;

	fp = fopen("/proc/mdstat", "r");
	if (!fp)
		return 0;

	while (fgets(line, sizeof(line), fp) != NULL) {

		/* i like things to be absolutely clean */
		memset(device, 0, 16);
		memset(separator, 0, 4);
		memset(status, 0, 16);
		memset(personality, 0, 16);
		memset(firstdevice, 0, 16);
		memset(devices, 0, 4096);

		if (strlen(line) > 4096)
			continue;

		/* we only parse stuff that starts with ^md
		 * that's supposed to point to raid */
		if (!(line[0] == 'm' && line[1] == 'd'))
			continue;

		sscanf(line, "%s %s %s %s %s",
		       device, separator, status, personality, firstdevice);

		/* scan only raids that are active */
		if (strcmp(status, "active"))
			continue;

		/* try to find *mdX and set the device as real raid.
		 * if we don't find the device we don't try to set the slaves */
		startnode = find_dev_by_path(devlisthead->devnode, device, 1);
		if (!startnode)
			continue;

		startnode->md = 1;

		/* trunkate the string from sdaX[Y] to sdaX and
		 * copy the whole device string over */
		memset(strstr(firstdevice, "["), 0, 1);
		strcpy(devices, strstr(line, firstdevice));

		/* if we don't find any slave (for whatever reason)
		 * keep going */
		if (!strlen(devices))
			continue;

		tmp = devices;
		while ((tmp) && ((next = strstr(tmp, " ")) || strlen(tmp))) {

			memset(strstr(tmp, "["), 0, 1);

			startnode =
			    find_dev_by_path(devlisthead->devnode, tmp, 1);
			if (startnode)
				startnode->md = 2;

			tmp = next;

			if (tmp)
				tmp++;

		}
	}

	fclose(fp);
	return 1;
}

/* scanmapper parses /proc/devices to identify what maj are associated
 * with device-mapper
 *
 * ret:
 * can't fail for now
 */
static int scanmapper(struct devlisthead *devlisthead)
{
	struct devnode *startnode;
	FILE *fp;
	char line[4096];
	char major[4];
	char device[64];
	int maj, start = 0;

	fp = fopen("/proc/devices", "r");
	if (!fp)
		return 0;

	while (fgets(line, sizeof(line), fp) != NULL) {
		memset(major, 0, 4);
		memset(device, 0, 64);

		if (strlen(line) > 4096)
			continue;

		if (!strncmp(line, "Block devices:", 13)) {
			start = 1;
			continue;
		}

		if (!start)
			continue;

		sscanf(line, "%s %s", major, device);

		if (!strncmp(device, "device-mapper", 13)) {
			maj = atoi(major);
			startnode = devlisthead->devnode;

			while (startnode) {
				if (startnode->maj == maj)
					startnode->mapper = 1;

				startnode = startnode->next;
			}

		}

	}

	fclose(fp);
	return 1;
}

/* scan through the list and execute the custom filter for each entry */
static void run_filter(struct devlisthead *devlisthead,
		       devfilter filter, void *filter_args)
{
	struct devnode *startnode = devlisthead->devnode;

	while (startnode) {
		filter(startnode, filter_args);
		startnode = startnode->next;
	}
	return;
}

/** sysfs helper functions **/

/* /sys/block/sda/dev or /sys/block/sda1/dev exists
 * the device is real and dev contains maj/min info.
 *
 * ret:
 * 1 on success and set maj/min
 * 0 if no file is found
 * -1 if we could not open the file
 */
static int sysfs_is_dev(char *path, int *maj, int *min)
{
	char newpath[MAXPATHLEN];
	struct stat sb;
	FILE *f;
	snprintf(newpath, sizeof(newpath), "%s/dev", path);
	if (!lstat(newpath, &sb)) {
		f = fopen(newpath, "r");
		if (f) {
			fscanf(f, "%d:%d", maj, min);
			fclose(f);
			return 1;
		} else
			return -1;
	}
	return 0;
}

/* /sys/block/sda/removable tells us if a device can be ejected
 * from the system or not. This is useful for USB pendrive that are
 * both removable and disks.
 *
 * ret:
 * 1 if is removable
 * 0 if not
 * -1 if we couldn't find the file.
 */
static int sysfs_is_removable(char *path)
{
	char newpath[MAXPATHLEN];
	struct stat sb;
	int i = -1;
	FILE *f;
	snprintf(newpath, sizeof(newpath), "%s/removable", path);
	if (!lstat(newpath, &sb)) {
		f = fopen(newpath, "r");
		if (f) {
			fscanf(f, "%d\n", &i);
			fclose(f);
		}
	}
	return i;
}

/* we use this function to scan /sys/block/sda{,1}/{holders,slaves}
 * to know in what position of the foodchain this device is.
 * NOTE: a device can have both holders and slaves at the same time!
 * (for example an lvm volume on top of a raid device made of N real disks
 *
 * ret:
 * always return the amount of entries in the dir if successful
 * or any return value from scandir.
 */
static int sysfs_has_subdirs_entries(char *path, char *subdir)
{
	char newpath[MAXPATHLEN];
	struct dirent **namelist;
	struct stat sb;
	int n, i, count = 0;

	snprintf(newpath, sizeof(newpath), "%s/%s", path, subdir);
	if (!lstat(newpath, &sb)) {
		if (S_ISDIR(sb.st_mode)) {
			i = scandir(newpath, &namelist, 0, alphasort);
			if (i < 0)
				return i;
			for (n = 0; n < i; n++) {
				if (namelist[n]->d_name[0] != '.')
					count++;
				free(namelist[n]);
			}
			free(namelist);
		}
	}
	return count;
}

/* this is the best approach so far to make sure a block device
 * is a disk and distinguish it from a cdrom or tape or etc.
 * What we know for sure is that a type 0 is a disk.
 * From an old piece code 0xe is an IDE disk and comes from media.
 * NOTE: we scan also for ../ that while it seems stupid, it will
 * allow to easily mark partitions as real disks.
 * (see for example /sys/block/sda/device/type and
 * /sys/block/sda1/../device/type)
 * TODO: there might be more cases to evaluate.
 *
 * ret:
 * -2 we were not able to open the file
 * -1 no path found
 *  0 we found the path but we have 0 clue on what it is
 *  1 is a disk
 */
static int sysfs_is_disk(char *path)
{
	char newpath[MAXPATHLEN];
	struct stat sb;
	int i = -1;
	FILE *f;

	snprintf(newpath, sizeof(newpath), "%s/device/type", path);
	if (!lstat(newpath, &sb))
		goto found;

	snprintf(newpath, sizeof(newpath), "%s/../device/type", path);
	if (!lstat(newpath, &sb))
		goto found;

	snprintf(newpath, sizeof(newpath), "%s/device/media", path);
	if (!lstat(newpath, &sb))
		goto found;

	snprintf(newpath, sizeof(newpath), "%s/../device/media", path);
	if (lstat(newpath, &sb))
		return -1;

      found:
	f = fopen(newpath, "r");
	if (f) {
		fscanf(f, "%d\n", &i);
		fclose(f);

		switch (i) {
		case 0x0:	/* scsi type_disk */
		case 0xe:	/* found on ide disks from old kernels.. */
			i = 1;
			break;
		default:
			i = 0;	/* by default we have no clue */
			break;
		}
	} else
		i = -2;

	return i;
}

/* recursive function that will scan and dive into /sys/block
 * looking for devices and scanning for attributes.
 *
 * ret:
 * 1 on success
 * -1 on generic error
 * -2 -ENOMEM
 */
static int scansysfs(struct devlisthead *devlisthead, char *path)
{
	struct devnode *startnode;
	int i, n, maj, min;
	struct dirent **namelist;
	struct stat sb;
	char newpath[MAXPATHLEN];

	i = scandir(path, &namelist, 0, alphasort);
	if (i < 0)
		return -1;

	for (n = 0; n < i; n++) {
		if (namelist[n]->d_name[0] != '.') {
			snprintf(newpath, sizeof(newpath),
				 "%s/%s", path, namelist[n]->d_name);
			if (!lstat(newpath, &sb)) {

				if (S_ISDIR(sb.st_mode))
					if (scansysfs(devlisthead, newpath) < 0)
						return -1;

				if (S_ISLNK(sb.st_mode))
					continue;

				if (sysfs_is_dev(newpath, &maj, &min) > 0) {
					startnode =
					    alloc_list_obj(devlisthead, maj,
							   min);
					if (!startnode)
						return -2;

					startnode->sysfsattrs.sysfs = 1;
					startnode->sysfsattrs.removable =
					    sysfs_is_removable(newpath);
					startnode->sysfsattrs.holders =
					    sysfs_has_subdirs_entries(newpath,
								      "holders");
					startnode->sysfsattrs.slaves =
					    sysfs_has_subdirs_entries(newpath,
								      "slaves");
					startnode->sysfsattrs.disk =
					    sysfs_is_disk(newpath);
				}
			}
		}
		free(namelist[n]);
	}

	free(namelist);
	return 1;
}

/*
 * devlisthead can be null if you are at init time. pass the old one if you are
 * updating or scanning..
 *
 * timeout is used only at init time to set the cache timeout value if default
 * value is not good enough. We might extend its meaning at somepoint.
 * Anything <= 0 means that the cache does not expire.
 */

struct devlisthead *scan_for_dev(struct devlisthead *devlisthead,
				 time_t timeout,
				 devfilter filter, void *filter_args)
{
	int res;
	time_t current;

	time(&current);

	if (devlisthead) {
		if ((current - devlisthead->cache_timestamp) <
		    devlisthead->cache_timeout) {
			return devlisthead;
		}
	} else {
		devlisthead = malloc(sizeof(struct devlisthead));
		if (!devlisthead)
			return NULL;
		memset(devlisthead, 0, sizeof(struct devlisthead));
		if (timeout)
			devlisthead->cache_timeout = timeout;
		else
			devlisthead->cache_timeout = DEVCACHETIMEOUT;
	}

	flush_dev_cache(devlisthead);
	devlisthead->cache_timestamp = current;

	/* it's important we check those 3 errors and abort in case
	 * as it means that we are running out of mem,
	 */
	devlisthead->sysfs = res = scansysfs(devlisthead, SYSBLOCKPATH);
	if (res < -1)
		goto emergencyout;

	devlisthead->procpart = res = scanprocpart(devlisthead);
	if (res < -1)
		goto emergencyout;

	devlisthead->lsdev = res = lsdev(devlisthead, DEVPATH);
	if (res < -1)
		goto emergencyout;

	/* from now on we don't alloc mem ourselves but only add info */
	devlisthead->mdstat = scanmdstat(devlisthead);
	devlisthead->mapper = scanmapper(devlisthead);
	if (filter)
		run_filter(devlisthead, filter, filter_args);

	return devlisthead;

      emergencyout:
	free_dev_list(devlisthead);
	return 0;
}

/* free everything we used so far */

void free_dev_list(struct devlisthead *devlisthead)
{
	if (devlisthead) {
		flush_dev_cache(devlisthead);
		free(devlisthead);
	}
	return;
}
