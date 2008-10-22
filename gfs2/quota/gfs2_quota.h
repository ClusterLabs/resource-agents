#ifndef __GFS2_QUOTA_DOT_H__
#define __GFS2_QUOTA_DOT_H__

#include "libgfs2.h"
#include "linux_endian.h"
#include <linux/gfs2_ondisk.h>

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
#define GQ_OP_RESET           (19)

#define GQ_ID_USER           (23)
#define GQ_ID_GROUP          (24)

#define GQ_UNITS_MEGABYTE    (0)
#define GQ_UNITS_KILOBYTE    (34)
#define GQ_UNITS_FSBLOCK     (35)
#define GQ_UNITS_BASICBLOCK  (36)

#define BUF_SIZE 4096

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

void do_get_super(int fd, struct gfs2_sb *sb);
void do_sync(struct gfs2_sbd *sdp, commandline_t *comline);
void lock_for_admin();
void mount_gfs2_meta();
void cleanup();
void read_superblock(struct gfs2_sb *sb, struct gfs2_sbd *sdp);
void get_last_quota_id(int fd, uint32_t *max_id);
int is_valid_quota_list(int fd);
inline void read_quota_internal(int fd, unsigned int id, int id_type, 
				struct gfs2_quota *q);
inline void write_quota_internal(int fd, unsigned int id, int id_type, 
				 struct gfs2_quota *q);
void print_quota_list_warning();

/*  check.c  */

void do_check(struct gfs2_sbd *sdp, commandline_t *comline);
void do_quota_init(struct gfs2_sbd *sdp, commandline_t *comline);

/*  names.c  */

uint32_t name_to_id(int user, char *name, int numbers);
char *id_to_name(int user, uint32_t id, int numbers);

#endif /* __GFS2_QUOTA_DOT_H__ */
