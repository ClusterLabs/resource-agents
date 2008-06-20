/**
  @file Quorum disk /proc/partition scanning functions
 */
#include <stdio.h>
#include <stdlib.h>
#include <disk.h>
#include <errno.h>
#include <sys/types.h>
#include <platform.h>
#include <stdlib.h>
#include <string.h>
#include <openais/service/logsys.h>
#include "scandisk.h"

struct device_args {
	char *label;
	struct devnode *devnode;
	int sector_size;
	int flags;
	int count;
	int pad;
};

LOGSYS_DECLARE_SUBSYS ("QDISK", LOG_LEVEL_INFO);

int
check_device(char *device, char *label, quorum_header_t *qh,
	     int flags)
{
	int ret = -1;
	quorum_header_t qh_local;
	target_info_t disk;

	if (!qh)
		qh = &qh_local;

	ret = qdisk_validate(device);
	if (ret < 0) {
		log_printf(LOG_DEBUG, "qdisk_verify");
		return -1;
	}

	ret = qdisk_open(device, &disk);
	if (ret < 0) {
		log_printf(LOG_ERR, "qdisk_open");
		return -1;
	}

	ret = -1;
	if (qdisk_read(&disk, OFFSET_HEADER, qh, sizeof(*qh)) == sizeof(*qh)) {
		swab_quorum_header_t(qh);
                if (qh->qh_magic == HEADER_MAGIC_NUMBER) {
			if (!label || !strcmp(qh->qh_cluster, label)) {
				ret = 0;
			}
                }
        }

	qh->qh_kernsz = disk.d_blksz;

	/* only flag now is 'strict device check'; i.e.,
	  "block size recorded must match kernel's reported size" */
	if (flags && qh->qh_version == VERSION_MAGIC_V2 &&
            disk.d_blksz != qh->qh_blksz) {
		ret = -1;
	}

	qdisk_close(&disk);

	return ret;
}


static void
filter_devs(struct devnode *node, void *v_args)
{
	struct device_args *args = (struct device_args *)v_args;
	quorum_header_t qh;
	quorum_header_t *ret_qh = NULL;
	int ret;

	if (!node->sysfsattrs.sysfs)
		return;
	if (!node->devpath)
		return;
	if (node->sysfsattrs.holders)
		return;
	/* Qdiskd doesn't work on soft-raid */
	if (node->md > 0)
		return;

	ret = check_device(node->devpath->path, args->label, &qh, args->flags);
	if (ret == 0) {
		ret_qh = malloc(sizeof(qh));
		if (!ret_qh)
			return;
		memcpy(ret_qh, &qh, sizeof(qh));

		node->filter = (void *)ret_qh;
		if (!args->count) {
			args->devnode = node;
		}
		++args->count;
	}
}


char *
state_str(disk_node_state_t s)
{
	switch (s) {
	case S_NONE:
		return "None";
	case S_EVICT:
		return "Evicted";
	case S_INIT:
		return "Initializing";
	case S_RUN:
		return "Running";
	case S_MASTER:
		return "Master";
	default:
		return "ILLEGAL";
	}
}


static void
print_status_block(status_block_t *sb)
{
	time_t timestamp = (time_t)sb->ps_timestamp;

	if (sb->ps_state == S_NONE)
		return;
	log_printf(LOG_INFO, "Status block for node %d\n", sb->ps_nodeid);
	log_printf(LOG_INFO, "\tLast updated by node %d\n", sb->ps_updatenode);
	log_printf(LOG_INFO, "\tLast updated on %s", ctime((time_t *)&timestamp));
	log_printf(LOG_INFO, "\tState: %s\n", state_str(sb->ps_state));
	log_printf(LOG_INFO, "\tFlags: %04x\n", sb->ps_flags);
	log_printf(LOG_INFO, "\tScore: %d/%d\n", sb->ps_score, sb->ps_scoremax);
	log_printf(LOG_INFO, "\tAverage Cycle speed: %d.%06d seconds\n", 
		sb->ps_ca_sec, sb->ps_ca_usec);
	log_printf(LOG_INFO, "\tLast Cycle speed: %d.%06d seconds\n", 
		sb->ps_lc_sec, sb->ps_lc_usec);
	log_printf(LOG_INFO, "\tIncarnation: %08x%08x\n",
		(int)(sb->ps_incarnation>>32&0xffffffff),
		(int)(sb->ps_incarnation&0xffffffff));

}


static void
read_info(char *dev)
{
	target_info_t ti;
	int x;
	status_block_t sb;

	if (qdisk_open(dev, &ti) < 0) {
		log_printf(LOG_ERR, "Could not read from %s: %s\n",
		       dev, strerror(errno));
		return;
	}

	for (x = 0; x < MAX_NODES_DISK; x++) {

		if (qdisk_read(&ti,
			       qdisk_nodeid_offset(x+1, ti.d_blksz),
			       &sb, sizeof(sb)) < 0) {
			log_printf(LOG_ERR, "Error reading node ID block %d\n",
			       x+1);
			continue;
		}
		swab_status_block_t(&sb);
		print_status_block(&sb);
	}

	qdisk_close(&ti);
}


static void
print_qdisk_info(struct devnode *dn)
{
	quorum_header_t *qh = (quorum_header_t *)dn->filter;
	struct devpath *dp;
	time_t timestamp = (time_t)qh->qh_timestamp;

	for (dp = dn->devpath; dp; dp = dp->next)
		log_printf(LOG_INFO, "%s:\n", dp->path);
	log_printf(LOG_INFO, "\tMagic:                %08x\n", qh->qh_magic);
	log_printf(LOG_INFO, "\tLabel:                %s\n", qh->qh_cluster);
	log_printf(LOG_INFO, "\tCreated:              %s", ctime(&timestamp));
	log_printf(LOG_INFO, "\tHost:                 %s\n", qh->qh_updatehost);
	log_printf(LOG_INFO, "\tKernel Sector Size:   %d\n", qh->qh_kernsz);
	if (qh->qh_version == VERSION_MAGIC_V2) {
		log_printf(LOG_INFO, "\tRecorded Sector Size: %d\n\n", (int)qh->qh_blksz);
	}
}

int
find_partitions(const char *label, char *devname, size_t devlen, int print)
{
	struct devlisthead *dh = NULL;
	struct devnode *dn = NULL;
	struct device_args dargs;

	memset(&dargs, 0, sizeof(dargs));
	dargs.label = (char *)label;
	dargs.flags = 1;	/* strict device check */
	dargs.devnode = NULL;	/* First matching device */

	dh = scan_for_dev(NULL, 5, filter_devs, (void *)(&dargs));
	if (!dh)
		goto not_found;
	if (!dargs.devnode)
		goto not_found;

	if (dargs.count > 0 && print) {
		for (dn = dh->devnode; dn; dn = dn->next) {
			if (dn->filter == NULL) {
				continue;
			}

			print_qdisk_info(dn);
			if (print >= 2) {
				/* Print node stuff */
				read_info(dn->devpath->path);
			}
		}
	}

	if (dargs.count == 1 && label) {
		snprintf(devname, devlen, "%s", dargs.devnode->devpath->path);
	}

	for (dn = dh->devnode; dn; dn = dn->next)
		if (dn->filter)
			free(dn->filter);
        free_dev_list(dh);

	if (print)
		/* No errors if we're just printing stuff */
		return 0;

	if (dargs.count == 1 || !label)
		return 0;

	/* more than one match */
	return 1;

   not_found:
        if (dh) {
		for (dn = dh->devnode; dn; dn = dn->next)
			if (dn->filter)
				free(dn->filter);
		free_dev_list(dh);
	}
	errno = ENOENT;
	return -1;
}
