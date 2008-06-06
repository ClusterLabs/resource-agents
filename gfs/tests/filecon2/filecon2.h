#ifndef __FILECON2_DOT_H__
#define __FILECON2_DOT_H__



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

#define do_lseek(fd, off) \
do \
{ \
  if (lseek((fd), (off), SEEK_SET) != (off)) \
    die("bad seek: %s on line %d of file %s\n", \
        strerror(errno),__LINE__, __FILE__); \
} \
while (0)

#define do_read(fd, buf, len) \
do \
{ \
  int do_read_out; \
  do_read_out = read((fd), (buf), (len)); \
  if (do_read_out != (len)) \
    die("bad read: result = %d (%s) on line %d of file %s\n", \
        do_read_out, strerror(errno), __LINE__, __FILE__); \
} \
while (0)

#define do_write(fd, buf, len) \
do \
{ \
  int do_write_out; \
  do_write_out = write((fd), (buf), (len)); \
  if (do_write_out != (len)) \
    die("bad write: result = %d (%s) on line %d of file %s\n", \
        do_write_out, strerror(errno), __LINE__, __FILE__); \
} \
while (0)

#define do_ftruncate(fd, off) \
do \
{ \
  if (ftruncate((fd), (off)) < 0) \
    die("bad truncate: %s on line %d of file %s\n", \
	strerror(errno), __LINE__, __FILE__); \
} \
while (0)

#define RAND(x) ((x) * (random() / (RAND_MAX + 1.0)))

#define DIV_RU(x, y) (((x) + (y) - 1) / (y))



#define FILECON2_PORT       (12046)
#define FILECON2_MAGIC      (0x76543210)

#define FCR_READ_BUFFERED   (16241)
#define FCR_READ_DIRECT     (16242)
#define FCR_READ_MMAPPED    (16243)
#define FCR_WRITE_BUFFERED  (16244)
#define FCR_WRITE_DIRECT    (16245)
#define FCR_WRITE_MMAPPED   (16246)
#define FCR_TRUNC           (16247)
#define FCR_NOP             (16248)
#define FCR_SEED            (16249)
#define FCR_STOP            (16250)

struct filecon2_request
{
  uint32 magic;
  uint32 type;
  uint32 length;
  uint32 data;
  uint64 offset;
};
typedef struct filecon2_request filecon2_request_t;

static __inline__ void request_in(filecon2_request_t *req, char *buf)
{
  filecon2_request_t *str = (filecon2_request_t *)buf;
  req->magic = be32_to_cpu(str->magic);
  req->type = be32_to_cpu(str->type);
  req->length = be32_to_cpu(str->length);
  req->data = be32_to_cpu(str->data);
  req->offset = be64_to_cpu(str->offset);
}

static __inline__ void request_out(filecon2_request_t *req, char *buf)
{
  filecon2_request_t *str = (filecon2_request_t *)buf;
  str->magic = cpu_to_be32(req->magic);
  str->type = cpu_to_be32(req->type);
  str->length = cpu_to_be32(req->length);
  str->data = cpu_to_be32(req->data);
  str->offset = cpu_to_be64(req->offset);
}


EXTERN char *prog_name;
EXTERN pid_t pid;


#endif  /*  __FILECON2_DOT_H__  */

