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

#ifndef __GFS2_QUOTA_DOT_H__
#define __GFS2_QUOTA_DOT_H__

#include "libgfs2.h"
#include "linux_endian.h"
#include <linux/gfs2_ondisk.h>

#ifndef TRUE
#define TRUE (1)
#endif

#ifndef FALSE
#define FALSE (0)
#endif

#define die(fmt, args...) \
do { \
	fprintf(stderr, "%s: ", prog_name); \
	fprintf(stderr, fmt, ##args); \
	exit(EXIT_FAILURE); \
} while (0)

#define type_zalloc(ptr, type, count) \
do { \
	(ptr) = (type *)malloc(sizeof(type) * (count)); \
	if ((ptr)) \
		memset((char *)(ptr), 0, sizeof(type) * (count)); \
	else \
		die("unable to allocate memory on line %d of file %s\n", \
		    __LINE__, __FILE__); \
} while (0)

#define type_alloc(ptr, type, count) \
do { \
	(ptr) = (type *)malloc(sizeof(type) * (count)); \
	if (!(ptr)) \
		die("unable to allocate memory on line %d of file %s\n", \
		    __LINE__, __FILE__); \
} while (0)

#define GQ_OP_LIST           (12)
#define GQ_OP_SYNC           (13)
#define GQ_OP_GET            (14)
#define GQ_OP_LIMIT          (15)
#define GQ_OP_WARN           (16)
#define GQ_OP_CHECK          (17)
#define GQ_OP_INIT           (18)

#define GQ_ID_USER           (23)
#define GQ_ID_GROUP          (24)

#define GQ_UNITS_MEGABYTE    (0)
#define GQ_UNITS_KILOBYTE    (34)
#define GQ_UNITS_FSBLOCK     (35)
#define GQ_UNITS_BASICBLOCK  (36)

#define BUF_SIZE 4096
#define meta_mount "/tmp/.gfs2meta"
char device_name[256];
char fspath[256];
char fsoptions[256];
//char meta_mount[PATH_MAX]; = "/tmp/.gfs2meta";
char metafs_path[BUF_SIZE];
int metafs_fd;
int metafs_mounted; /* If metafs was already mounted */

struct commandline {
	unsigned int operation;

	uint64_t new_value;
	int new_value_set;

	unsigned int id_type;
	uint32_t id;

	unsigned int units;

	int numbers;

	char filesystem[PATH_MAX];
};
typedef struct commandline commandline_t;

extern char *prog_name;

/*  main.c  */

void check_for_gfs2(const char *path);
void do_get_super(int fd, struct gfs2_sb *sb);
void do_sync(commandline_t *comline);
void lock_for_admin();
int find_gfs2_meta(const char *mnt);
void mount_gfs2_meta();
void cleanup();
void read_superblock(struct gfs2_sb *sb);

/*  check.c  */

void do_check(commandline_t *comline);
void do_quota_init(commandline_t *comline);

/*  names.c  */

uint32_t name_to_id(int user, char *name, int numbers);
char *id_to_name(int user, uint32_t id, int numbers);


static inline int __do_read(int fd, char *buff, size_t len, 
			    const char *file, int line)
{
	int ret = read(fd, buff, len);
	if (ret < 0) {
		die("bad read: %s on line %d of file %s\n", 
		    strerror(errno), line, file);
	}
	return ret;
}

#define do_read(fd, buf, len) \
	__do_read((fd), (buf), (len), __FILE__, __LINE__)

static inline int __do_write(int fd, char *buff, size_t len,
			     const char *file, int line)
{
	int ret = write(fd, buff, len);
	if (ret != len) {
		die("bad write: %s on line %d of file %s\n",
		    strerror(errno), line, file);
	}
	return ret;
}

#define do_write(fd, buf, len) \
	__do_write((fd), (buf), (len), __FILE__, __LINE__)

static inline int __do_lseek(int fd, off_t off, const char *file, int line)
{
	if (lseek(fd, off, SEEK_SET) != off) {
		die("bad seek: %s on line %d of file %s\n",
		    strerror(errno), line, file);
	}
	return 0;
}

#define do_lseek(fd, off) \
	__do_lseek((fd), (off), __FILE__, __LINE__)

#endif /* __GFS2_QUOTA_DOT_H__ */
