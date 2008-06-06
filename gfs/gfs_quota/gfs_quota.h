#ifndef __GFS_QUOTA_DOT_H__
#define __GFS_QUOTA_DOT_H__


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

struct commandline {
	unsigned int operation;

	uint64_t new_value;
	int new_value_set;

	unsigned int id_type;
	uint32_t id;

	unsigned int units;

	int no_hidden_file_blocks;
	int numbers;

	char filesystem[PATH_MAX];
};
typedef struct commandline commandline_t;

extern char *prog_name;

/*  main.c  */

void check_for_gfs(int fd, char *path);
void do_get_super(int fd, struct gfs_sb *sb);
void do_sync(commandline_t *comline);
uint64_t compute_hidden_blocks(commandline_t *comline, int fd);

/*  check.c  */

void do_check(commandline_t *comline);
void do_init(commandline_t *comline);

/*  names.c  */

uint32_t name_to_id(int user, char *name, int numbers);
char *id_to_name(int user, uint32_t id, int numbers);

/* layout.c */

void print_quota_file(commandline_t *comline);
#endif /* __GFS_QUOTA_DOT_H__ */
