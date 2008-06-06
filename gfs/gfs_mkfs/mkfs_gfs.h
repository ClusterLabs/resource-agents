#ifndef __MKFS_GFS_DOT_H__
#define __MKFS_GFS_DOT_H__


/*  Extern Macro  */

#ifndef EXTERN
#define EXTERN extern
#define INIT(X)
#else
#undef EXTERN
#define EXTERN
#define INIT(X) =X 
#endif


#define die(fmt, args...) \
do \
{ \
  fprintf(stderr, "%s: ", prog_name); \
  fprintf(stderr, fmt, ##args); \
  exit(EXIT_FAILURE); \
} \
while (0)

#define MKFS_ASSERT(x, todo) \
do \
{ \
  if (!(x)) \
  { \
    {todo} \
    die("assertion failed on line %d of file %s\n", __LINE__, __FILE__); \
  } \
} \
while (0)

#define type_zalloc(ptr, type, count) \
do \
{ \
  (ptr) = (type *)malloc(sizeof(type) * (count)); \
  if ((ptr)) \
    memset((char *)(ptr), 0, sizeof(type) * (count)); \
  else \
    die("unable to allocate memory on line %d of file %s\n", \
	__LINE__, __FILE__); \
} \
while (0)

#define type_alloc(ptr, type, count) \
do \
{ \
  (ptr) = (type *)malloc(sizeof(type) * (count)); \
  if (!(ptr)) \
    die("unable to allocate memory on line %d of file %s\n", \
	__LINE__, __FILE__); \
} \
while (0)

#define DIV_RU(x, y) (((x) + (y) - 1) / (y))





struct mkfs_subdevice
{
  uint64 start;
  uint64 length;
  int is_journal;
};
typedef struct mkfs_subdevice mkfs_subdevice_t;


struct mkfs_device
{
  unsigned int nsubdev;
  mkfs_subdevice_t *subdev; 
};
typedef struct mkfs_device mkfs_device_t;


struct commandline
{
  char lockproto[GFS_LOCKNAME_LEN];
  char locktable[GFS_LOCKNAME_LEN];

  uint32 bsize;             /*  The block size of the FS  */
  uint32 seg_size;          /*  The journal segment size  */
  uint32 journals;          /*  Number of journals  */
  uint32 jsize;             /*  Size of journals  */
  uint32 rgsize;            /*  The Resource Group size  */
  int rgsize_specified;     /*  Did the user specify a rg size? */

  int debug;                /*  Print out debugging information?  */
  int quiet;                /*  No messages  */
  int expert;
  int override;

  char *device;             /*  device  */


  /*  Not specified on the command line, but...  */

  uint64 rgrps;             /*  Number of rgrps  */
  int fd;                   /*  fd of the device  */
  struct gfs_sbd *sbd;      /*  A copy of the superblock */
  uint64 fssize;            /*  size of the filesystem  */

  uint64 sb_addr;

  uint64 rgrp0_next;        /*  The address of the next available block in rgrp 0  */
};
typedef struct commandline commandline_t;


struct rgrp_list
{
  osi_list_t list;

  uint32 subdevice;         /*  The subdevice who holds this resource group  */

  uint64 rg_offset;         /*  The offset of the beginning of this resource group  */
  uint64 rg_length;         /*  The length of this resource group  */

  struct gfs_rindex *ri;
};
typedef struct rgrp_list rgrp_list_t;


struct journal_list
{
  osi_list_t list;

  uint64 start;
  uint32 segments;
};
typedef struct journal_list journal_list_t;


EXTERN char *prog_name;


#define MKFS_DEFAULT_BSIZE          (4096)
#define MKFS_DEFAULT_SEG_SIZE       (16)
#define MKFS_DEFAULT_JSIZE          (128)
#define MKFS_DEFAULT_RGSIZE         (256)
#define MKFS_DEFAULT_LOCKPROTO      "lock_dlm"
#define MKFS_EXCESSIVE_RGS          (10000)

/*  device_geometry.c  */

void device_geometry(commandline_t *comline, mkfs_device_t *device);
void add_journals_to_device(commandline_t *comline, mkfs_device_t *device);
void fix_device_geometry(commandline_t *comline, mkfs_device_t *device);


/*  fs_geometry.c  */

void compute_rgrp_layout(commandline_t *comline, mkfs_device_t *device, osi_list_t *rlist);
void compute_journal_layout(commandline_t *comline, mkfs_device_t *device, osi_list_t *jlist);


/*  locking.c  */

int test_locking(char *lockproto, char *locktable, char *estr, unsigned int elen);


/*  structures.c  */

void write_mkfs_sb(commandline_t *comline, osi_list_t *rlist);
void write_jindex(commandline_t *comline, osi_list_t *jlist);
void write_rindex(commandline_t *comline, osi_list_t *rlist);
void write_root(commandline_t *comline);
void write_quota(commandline_t *comline);
void write_license(commandline_t *comline);
void write_rgrps(commandline_t *comline, osi_list_t *rlist);
void write_journals(commandline_t *comline, osi_list_t *jlist);


#endif  /*  __MKFS_GFS_DOT_H__  */

