#ifndef __GFS_DEBUG_DOT_H__
#define __GFS_DEBUG_DOT_H__


#ifndef TRUE
#define TRUE (1)
#endif

#ifndef FALSE
#define FALSE (0)
#endif

#ifndef EXTERN
#define EXTERN extern
#define INIT(X)
#else
#undef EXTERN
#define EXTERN
#define INIT(X) =X 
#endif


#define die(fmt, args...) \
do { \
	fprintf(stderr, "%s: ", prog_name); \
	fprintf(stderr, fmt, ##args); \
	exit(EXIT_FAILURE); \
} while (0)

#define ASSERT(x, todo) \
do { \
	if (!(x)) { \
		{todo} \
		die("assertion failed on line %d of file %s\n", \
		    __LINE__, __FILE__); \
	} \
} while (0)

EXTERN char *prog_name;

#define do_lseek(fd, off) \
do { \
	if (lseek((fd), (off), SEEK_SET) != (off)) \
		die("bad seek on line %d of file %s: %s\n", \
		    __LINE__, __FILE__, strerror(errno)); \
} while (0)

#define do_read(fd, buff, len) \
do { \
	if (read((fd), (buff), (len)) != (len)) \
		die("bad read on line %d of file %s: %s\n", \
		    __LINE__, __FILE__, strerror(errno)); \
} while (0)

#define do_write(fd, buff, len) \
do { \
	if (write((fd), (buff), (len)) != (len)) \
		die("bad write on line %d of file %s: %s\n", \
		    __LINE__, __FILE__, strerror(errno)); \
} while (0)

#define DIV_RU(x, y) (((x) + (y) - 1) / (y))


/* Command line arguments */

EXTERN unsigned int verbose INIT(0);

EXTERN char *action INIT(NULL);

EXTERN char *device INIT(NULL);
EXTERN int device_fd INIT(-1);
EXTERN off_t device_size INIT(-1);

EXTERN int is_gfs INIT(FALSE);
EXTERN unsigned int block_size INIT(0);
EXTERN unsigned int block_size_shift INIT(0);

EXTERN uint64_t block_number INIT(0);


#endif /* __GFS_DEBUG_DOT_H__ */

