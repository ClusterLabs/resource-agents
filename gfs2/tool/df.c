#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <inttypes.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <linux/types.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <limits.h>
#include <errno.h>

#define __user
#include <linux/gfs2_ondisk.h>

#include "gfs2_tool.h"
#include "libgfs2.h"

/**
 * do_df_one - print out information about one filesystem
 * @path: the path to the filesystem
 *
 */

static void
do_df_one(char *path)
{
	unsigned int percentage;
	unsigned int journals;
	uint64_t rgrps;
	unsigned int flags;
	char *value, *fs;
 	int x, statfs_fd;
	struct gfs2_sbd sbd;
	char buf[GFS2_DEFAULT_BSIZE], statfs_fn[PATH_MAX];
	struct gfs2_statfs_change sc;

	memset(&sbd, 0, sizeof(struct gfs2_sbd));
	sbd.path_name = path;
	check_for_gfs2(&sbd);
	fs = mp2fsname(sbd.path_name);

	sbd.device_fd = open(sbd.device_name, O_RDONLY);
	if (sbd.device_fd < 0)
		die("can't open %s: %s\n", path, strerror(errno));

	sbd.bsize = GFS2_DEFAULT_BSIZE;
	sbd.jsize = GFS2_DEFAULT_JSIZE;
	sbd.rgsize = GFS2_DEFAULT_RGSIZE;
	sbd.utsize = GFS2_DEFAULT_UTSIZE;
	sbd.qcsize = GFS2_DEFAULT_QCSIZE;
	osi_list_init(&sbd.rglist);
	osi_list_init(&sbd.buf_list);
	for (x = 0; x < BUF_HASH_SIZE; x++)
		osi_list_init(&sbd.buf_hash[x]);
	do_lseek(sbd.device_fd, 0x10 * sbd.bsize);
	do_read(sbd.device_fd, buf, sbd.bsize); /* read in the superblock */

	compute_constants(&sbd);
	gfs2_sb_in(&sbd.sd_sb, buf); /* parse it out into the sb structure */

	sbd.master_dir = gfs2_load_inode(&sbd,
					 sbd.sd_sb.sb_master_dir.no_addr);

	gfs2_lookupi(sbd.master_dir, "rindex", 6, &sbd.md.riinode);
	gfs2_lookupi(sbd.master_dir, "jindex", 6, &sbd.md.jiinode);
	close(sbd.device_fd);

	journals = sbd.md.jiinode->i_di.di_entries - 2;

	rgrps = sbd.md.riinode->i_di.di_size;
	if (rgrps % sizeof(struct gfs2_rindex))
		die("bad rindex size\n");
	rgrps /= sizeof(struct gfs2_rindex);

	printf("%s:\n", path);
	printf("  SB lock proto = \"%s\"\n", sbd.sd_sb.sb_lockproto);
	printf("  SB lock table = \"%s\"\n", sbd.sd_sb.sb_locktable);
	printf("  SB ondisk format = %u\n", sbd.sd_sb.sb_fs_format);
	printf("  SB multihost format = %u\n", sbd.sd_sb.sb_multihost_format);
	printf("  Block size = %u\n", sbd.sd_sb.sb_bsize);
	printf("  Journals = %u\n", journals);
	printf("  Resource Groups = %"PRIu64"\n", rgrps);
	printf("  Mounted lock proto = \"%s\"\n",
	       ((value = get_sysfs(fs, "args/lockproto"))[0])
	       ? value : sbd.sd_sb.sb_lockproto);
	printf("  Mounted lock table = \"%s\"\n",
	       ((value = get_sysfs(fs, "args/locktable"))[0])
	       ? value : sbd.sd_sb.sb_locktable);
	printf("  Mounted host data = \"%s\"\n",
	       get_sysfs(fs, "args/hostdata"));
	printf("  Journal number = %s\n", get_sysfs(fs, "lockstruct/jid"));
	flags = get_sysfs_uint(fs, "lockstruct/flags");
	printf("  Lock module flags = %x", flags);
	printf("\n");
	printf("  Local flocks = %s\n",
	       (get_sysfs_uint(fs, "args/localflocks")) ? "TRUE" : "FALSE");
	printf("  Local caching = %s\n",
		(get_sysfs_uint(fs, "args/localcaching")) ? "TRUE" : "FALSE");

	/* Read the master statfs file */
	if (!find_gfs2_meta(&sbd))
		mount_gfs2_meta(&sbd);
	lock_for_admin(&sbd);

	sprintf(statfs_fn, "%s/statfs", sbd.metafs_path);
	statfs_fd = open(statfs_fn, O_RDONLY);
	do_read(statfs_fd, buf, sizeof(struct gfs2_statfs_change));
	gfs2_statfs_change_in(&sc, (char *)&buf);

	close(statfs_fd);

	cleanup_metafs(&sbd);

	printf("\n");
	printf("  %-15s%-15s%-15s%-15s%-15s\n", "Type", "Total", "Used", "Free", "use%");
	printf("  ------------------------------------------------------------------------\n");

	percentage = sc.sc_total ?
		(100.0 * (sc.sc_total - sc.sc_free)) / sc.sc_total + 0.5 : 0;
	printf("  %-15s%-15llu%-15llu%-15llu%u%%\n", "data",
	       sc.sc_total, sc.sc_total - sc.sc_free, sc.sc_free, percentage);

	percentage = (sc.sc_dinodes + sc.sc_free) ?
		(100.0 * sc.sc_dinodes / (sc.sc_dinodes + sc.sc_free)) + 0.5 :
		0;
	printf("  %-15s%-15llu%-15llu%-15llu%u%%\n", "inodes",
	       sc.sc_dinodes + sc.sc_free, sc.sc_dinodes,
	       sc.sc_free, percentage);
}


/**
 * print_df - print out information about filesystems
 * @argc:
 * @argv:
 *
 */

void
print_df(int argc, char **argv)
{
	if (optind < argc) {
		char buf[PATH_MAX];

		if (!realpath(argv[optind], buf))
			die("can't determine real path: %s\n", strerror(errno));

		do_df_one(buf);

		return;
	}

	{
		FILE *file;
		char buf[256], device[256], path[256], type[256];
		int first = TRUE;

		file = fopen("/proc/mounts", "r");
		if (!file)
			die("can't open /proc/mounts: %s\n", strerror(errno));

		while (fgets(buf, 256, file)) {
			if (sscanf(buf, "%s %s %s", device, path, type) != 3)
				continue;
			if (strcmp(type, "gfs2") != 0)
				continue;

			if (first)
				first = FALSE;
			else
				printf("\n");

			do_df_one(path);
		}

		fclose(file);
	}
}
