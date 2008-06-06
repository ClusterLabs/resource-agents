#ifndef __MOUNT_DOT_H__
#define __MOUNT_DOT_H__

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <inttypes.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mount.h>
#include <fcntl.h>
#include <unistd.h>
#include <limits.h>
#include <dirent.h>
#include <errno.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <linux/types.h>
#include <linux/gfs2_ondisk.h>
#include "gfs_ondisk.h"
#include "linux_endian.h"

#define die(fmt, args...) \
do { \
	fprintf(stderr, "%s: ", prog_name); \
	fprintf(stderr, fmt, ##args); \
	exit(EXIT_FAILURE); \
} while (0)

#define warn(fmt, args...) \
do { \
	fprintf(stderr, "%s: " fmt "\n", prog_name, ##args); \
} while (0)

#define log_debug(fmt, args...) \
do { \
	if (verbose) \
		printf("%s: " fmt "\n", prog_name, ##args); \
} while (0)

#define do_lseek(fd, off) \
do { \
	if (lseek((fd), (off), SEEK_SET) != (off)) \
		die("bad seek: %s on line %d of file %s\n", \
		    strerror(errno),__LINE__, __FILE__); \
} while (0)

#define do_read(fd, buff, len) \
do { \
	if (read((fd), (buff), (len)) != (len)) \
		die("bad read: %s on line %d of file %s\n", \
		    strerror(errno), __LINE__, __FILE__); \
} while (0)

struct mount_options {
	char dev[PATH_MAX+1];
	char dir[PATH_MAX+1];
	char opts[PATH_MAX+1];
	char hostdata[PATH_MAX+1];
	char extra[PATH_MAX+1];
	char extra_plus[PATH_MAX+1];
	char type[5]; 
	char lockproto[256];
	char locktable[256];
	char proc_entry[PATH_MAX+1];
	int flags;
};

struct gen_sb {
	char lockproto[256];
	char locktable[256];
};

/* util.c */

char *select_lockproto(struct mount_options *mo, struct gen_sb *sb);
void parse_opts(struct mount_options *mo);
void read_proc_mounts(struct mount_options *mo);
int get_sb(char *device, struct gen_sb *sb_out);
int lock_dlm_join(struct mount_options *mo, struct gen_sb *sb);
void lock_dlm_mount_done(struct mount_options *mo, struct gen_sb *sb, int result);
int lock_dlm_leave(struct mount_options *mo, struct gen_sb *sb, int mnterr);
int lock_dlm_remount(struct mount_options *mo, struct gen_sb *sb);

/* mtab.c */

void add_mtab_entry(struct mount_options *mo);
void del_mtab_entry(struct mount_options *mo);

#endif

