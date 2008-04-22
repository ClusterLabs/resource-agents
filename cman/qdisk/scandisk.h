/******************************************************************************
*******************************************************************************
**
**  Copyright (C) 2008 Red Hat, Inc.  All rights reserved.
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

#ifndef __SCANDISK_H__
#define __SCANDISK_H__

#ifndef DEVPATH
#define	DEVPATH		"/dev"
#endif

#ifndef SYSFSPATH
#define SYSFSPATH	"/sys"
#endif

#ifndef SYSBLOCKPATH
#define SYSBLOCKPATH	SYSFSPATH "/block"
#endif

#ifdef DEBUG
#define	DEVCACHETIMEOUT	5	/* expressed in seconds */
#else
#define	DEVCACHETIMEOUT	30
#endif

/* each entry can be (generally):
 * > 0 on success or good hit
 * 0 on success with no hit
 * < 0 on error
 */

struct sysfsattrs {		/* usual 0 | 1 game */
	int sysfs;		/* did we find an entry in sysfs at all? */
	int slaves;		/* device has slaves */
	int holders;		/* device has holders */
	int removable;		/* device is removable */
	int disk;		/* device is a disk */
};

/* this structure is required because we don't know upfront how many
 * entries for a certain maj/min will be found in /dev, and so we need
 * to alloc them dynamically.
 */
struct devpath {
	struct devpath *next;
	char path[MAXPATHLEN];
};

/* this structure holds all the data for each maj/min found in the system
 * that is a block device
 */
struct devnode {
	struct devnode *next;
	struct devpath *devpath;	/* point to the first path entry */
	int maj;		/* device major */
	int min;		/* device minor */
	struct sysfsattrs sysfsattrs;	/* like the others.. scanning /sys */
	int procpart;		/* 0 if the device is not in proc/part or 1 on success. <0 on error */
	char procname[MAXPATHLEN];	/* non-NULL if we find a maj/min match */
	int md;			/* 0 nothing to do with raid, 1 is raid,
				 * 2 is raid slave - data from /proc/mdstat */
	int mapper;		/* 0 nothing, 1 we believe it's a devmap dev */
	void *filter;		/* your filter output.. whatever it is */
};

/* this is what you get after a scan... if you are lucky */
/* each entry can be 0 if we can't scan or < 0 if there are errors */

struct devlisthead {
	time_t cache_timestamp;	/* this cache timestamp */
	int cache_timeout;	/* for how long this cache is valid */
	int sysfs;		/* set to 1 if we were able to scan
				 * /sys */
	int procpart;		/* set to 1 if we were able to scan
				 * /proc/partitions */
	int lsdev;		/* set to 1 if we were able to ls /dev */
	int mdstat;		/* set to 1 if we were able to scan
				 * /proc/mdstat */
	int mapper;		/* set to 1 if we were able to run
				 * something against mapper */
	struct devnode *devnode;	/* points to the first entry */
};

typedef void (*devfilter) (struct devnode * cur, void *arg);

struct devlisthead *scan_for_dev(struct devlisthead *devlisthead,
				 time_t timeout,
				 devfilter filter, void *filter_args);
void free_dev_list(struct devlisthead *devlisthead);

#endif /* __SCANDISK_H__ */
