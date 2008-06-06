#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <time.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>

#include "global.h"
#include "linux_endian.h"
#define EXTERN
#include "filecon2.h"



#ifndef O_DIRECT
#define O_DIRECT (0)
#warning O_DIRECT is broken
#endif
#ifndef PAGE_SIZE
#define PAGE_SIZE (65536)
#endif

#define CHANCES (8)

#define PATTERN_RAND      (12341)
#define PATTERN_OPNUM     (12342)



struct operation
{
  char *name;
  void (*func)();
};
typedef struct operation operation_t;



unsigned int port = FILECON2_PORT;
unsigned int seed = 0;
uint64 offset = 0;
uint64 length = 1048576;
uint32 chunk = 1024;
unsigned int pattern = PATTERN_RAND;
int buffered = FALSE;
int direct = FALSE;
int mmapped = FALSE;  
int trunc = FALSE;
int nop = FALSE;
unsigned int align = 1;
int reread = FALSE;
int respect_eof = FALSE;
int check_file_size = FALSE;
int verbose = FALSE;
char *servername = NULL;
char *filename = NULL;

int user_seed = FALSE;
int fd_b = -1, fd_d = -1;
int sock;

int first = FALSE;
unsigned char *mmap_data = NULL;

uint64 file_size;
unsigned int opnum;

float chances[CHANCES];





void rand_extent(uint64 *o, uint32 *l)
{
  uint64 off;
  uint32 len;

  if (align > 1)
  {
    do
    {
      off = RAND(length / align);
      len = RAND(chunk / align) + 1;
      off *= align;
      len *= align;
    }
    while (off + len > length);
  }
  else
  {
    do
    {
      off = RAND(length);
      len = RAND(chunk) + 1;
    }
    while (off + len > length);
  }

  *o = off;
  *l = len;
}


void fill_pattern(unsigned char *data, unsigned int len)
{
  unsigned int x;

  switch (pattern)
  {
  case PATTERN_RAND:
    while (len--)
      *data++ = RAND(256);
    break;

  case PATTERN_OPNUM:
    x = cpu_to_be32(opnum);
    while (len)
    {
      if (len > sizeof(unsigned int))
      {
	memcpy(data, &x, sizeof(unsigned int));
	data += sizeof(unsigned int);
	len -= sizeof(unsigned int);
      }
      else
      {
	memcpy(data, &x, len);
	break;
      }
    }
    break;

  default:
    die("unknown pattern\n");
  }
}


void do_first()
{
  filecon2_request_t req;
  unsigned char data[sizeof(filecon2_request_t) + 1];

  memset(&req, 0, sizeof(filecon2_request_t));
  req.magic = FILECON2_MAGIC;
  req.type = FCR_WRITE_BUFFERED;
  req.length = 1;
  req.data = 1;
  req.offset = offset + length - 1;

  fill_pattern(data + sizeof(filecon2_request_t), 1);

  do_lseek(fd_b, offset + length - 1);
  do_write(fd_b, data + sizeof(filecon2_request_t), 1);

  request_out(&req, data);
  do_write(sock, data, sizeof(filecon2_request_t) + 1);

  mmap_data = mmap(NULL, length, PROT_READ | PROT_WRITE, MAP_SHARED, fd_b, offset);
  if (mmap_data == MAP_FAILED)
    die("can't mmap file: %s\n", strerror(errno));

  first = FALSE;
}


void do_check_file_size()
{
  struct stat st;
  int error;

  error = fstat(fd_b, &st);
  if (error)
    die("can't stat file: %s\n", strerror(errno));

  if (file_size != st.st_size)
    die("file size mismatch (server = %"PRIu64", me = %"PRIu64")\n",
	file_size, st.st_size);
}


void op_buffered_read()
{
  filecon2_request_t req;
  uint64 off;
  uint32 len;
  unsigned char *data;
  int error;

  rand_extent(&off, &len);
  off += offset;

  memset(&req, 0, sizeof(filecon2_request_t));
  req.magic = FILECON2_MAGIC;
  req.type = FCR_READ_BUFFERED;
  req.length = len;
  req.data = len;
  req.offset = off;

  type_alloc(data, char, sizeof(filecon2_request_t) + len);

  do_lseek(fd_b, off);
  error = read(fd_b, data + sizeof(filecon2_request_t), len);
  if (error < 0)
    die("error reading from file: %s\n", strerror(errno));
  req.data = error;

  request_out(&req, data);
  do_write(sock, data, sizeof(filecon2_request_t) + req.data);

  free(data);
}


void op_buffered_write()
{
  filecon2_request_t req;
  uint64 off;
  uint32 len;
  unsigned char *data;

  rand_extent(&off, &len);
  off += offset;

  memset(&req, 0, sizeof(filecon2_request_t));
  req.magic = FILECON2_MAGIC;
  req.type = FCR_WRITE_BUFFERED;
  req.length = len;
  req.data = len;
  req.offset = off;

  type_alloc(data, char, sizeof(filecon2_request_t) + len);

  fill_pattern(data + sizeof(filecon2_request_t), len);

  do_lseek(fd_b, off);
  do_write(fd_b, data + sizeof(filecon2_request_t), len);

  if (reread)
  {
    unsigned char *data_reread;

    type_alloc(data_reread, char, len);

    do_lseek(fd_b, off);
    do_read(fd_b, data_reread, len);

    if (memcmp(data + sizeof(filecon2_request_t), data_reread, len) != 0)
      die("buffered write: bad reread\n");

    free(data_reread);
  }

  request_out(&req, data);
  do_write(sock, data, sizeof(filecon2_request_t) + req.data);

  free(data);
}


void op_direct_read()
{
  filecon2_request_t req;
  uint64 off;
  uint32 len;
  unsigned char *data, *data_orig;
  unsigned int remainder;
  int error;

  if (respect_eof)
  {
    if (file_size < offset + align)
    {
      off = offset;
      len = 0;
    }
    else
      for (;;)
      {
	rand_extent(&off, &len);
	off += offset;
	if (off + len <= file_size)
	  break;
      }
  }
  else
  {
    rand_extent(&off, &len);
    off += offset;
  }

  memset(&req, 0, sizeof(filecon2_request_t));
  req.magic = FILECON2_MAGIC;
  req.type = FCR_READ_DIRECT;
  req.length = len;
  req.data = len;
  req.offset = off;

  data = data_orig = malloc(sizeof(filecon2_request_t) + len + PAGE_SIZE);
  if (!data)
    die("out of memory\n");
  remainder = ((unsigned long)(data + sizeof(filecon2_request_t))) & (PAGE_SIZE - 1);
  if (remainder)
    data += PAGE_SIZE - remainder;

  do_lseek(fd_d, off);
  error = read(fd_d, data + sizeof(filecon2_request_t), len);
  if (error < 0)
    die("error reading from file: %s\n", strerror(errno));
  req.data = error;

  request_out(&req, data);
  do_write(sock, data, sizeof(filecon2_request_t) + req.data);

  free(data_orig);
}


void op_direct_write()
{
  filecon2_request_t req;
  uint64 off;
  uint32 len;
  unsigned char *data, *data_orig;
  unsigned int remainder;

  rand_extent(&off, &len);
  off += offset;

  memset(&req, 0, sizeof(filecon2_request_t));
  req.magic = FILECON2_MAGIC;
  req.type = FCR_WRITE_DIRECT;
  req.length = len;
  req.data = len;
  req.offset = off;

  data = data_orig = malloc(sizeof(filecon2_request_t) + len + PAGE_SIZE);
  if (!data)
    die("out of memory\n");
  remainder = ((unsigned long)(data + sizeof(filecon2_request_t))) & (PAGE_SIZE - 1);
  if (remainder)
    data += PAGE_SIZE - remainder;

  fill_pattern(data + sizeof(filecon2_request_t), len);

  do_lseek(fd_d, off);
  do_write(fd_d, data + sizeof(filecon2_request_t), len);

  if (reread)
  {
    unsigned char *data_reread, *data_reread_orig;

    data_reread = data_reread_orig = malloc(len + PAGE_SIZE);
    if (!data_reread)
      die("out of memory\n");
    remainder = ((unsigned long)data_reread) & (PAGE_SIZE - 1);
    if (remainder)
      data_reread += PAGE_SIZE - remainder;

    do_lseek(fd_d, off);
    do_read(fd_d, data_reread, len);

    if (memcmp(data + sizeof(filecon2_request_t), data_reread, len) != 0)
      die("direct write: bad reread\n");

    free(data_reread_orig);
  }

  request_out(&req, data);
  do_write(sock, data, sizeof(filecon2_request_t) + req.data);

  free(data_orig);
}


void op_mmapped_read()
{
  filecon2_request_t req;
  uint64 off;
  uint32 len;
  unsigned char *data;

  rand_extent(&off, &len);

  memset(&req, 0, sizeof(filecon2_request_t));
  req.magic = FILECON2_MAGIC;
  req.type = FCR_READ_MMAPPED;
  req.length = len;
  req.data = len;
  req.offset = offset + off;

  type_alloc(data, char, sizeof(filecon2_request_t) + len);

  memcpy(data + sizeof(filecon2_request_t), mmap_data + off, len);

  request_out(&req, data);
  do_write(sock, data, sizeof(filecon2_request_t) + req.data);

  free(data);
}


void op_mmapped_write()
{
  filecon2_request_t req;
  uint64 off;
  uint32 len;
  unsigned char *data;

  rand_extent(&off, &len);

  memset(&req, 0, sizeof(filecon2_request_t));
  req.magic = FILECON2_MAGIC;
  req.type = FCR_WRITE_MMAPPED;
  req.length = len;
  req.data = len;
  req.offset = offset + off;

  type_alloc(data, char, sizeof(filecon2_request_t) + len);

  fill_pattern(data + sizeof(filecon2_request_t), len);

  memcpy(mmap_data + off, data + sizeof(filecon2_request_t), len);

  if (reread)
  {
    if (memcmp(mmap_data + off, data + sizeof(filecon2_request_t), len) != 0)
      die("mmapped write: bad reread\n");
  }

  request_out(&req, data);
  do_write(sock, data, sizeof(filecon2_request_t) + req.data);

  free(data);
}


void op_trunc()
{
  filecon2_request_t req;
  uint64 off;
  unsigned char data[sizeof(filecon2_request_t)];

  if (align > 1)
  {
    off = RAND((length / align) + 1);
    off *= align;
  }
  else
    off = RAND(length + 1);

  off += offset;

  memset(&req, 0, sizeof(filecon2_request_t));
  req.magic = FILECON2_MAGIC;
  req.type = FCR_TRUNC;
  req.offset = off;

  do_ftruncate(fd_b, off);

  request_out(&req, data);
  do_write(sock, data, sizeof(filecon2_request_t));
}


void op_nop()
{
  filecon2_request_t req;
  unsigned char data[sizeof(filecon2_request_t)];

  memset(&req, 0, sizeof(filecon2_request_t));
  req.magic = FILECON2_MAGIC;
  req.type = FCR_NOP;

  request_out(&req, data);
  do_write(sock, data, sizeof(filecon2_request_t));
}


const static operation_t ops[] =
{
  { "buffered read", op_buffered_read },
  { "buffered write", op_buffered_write },
  { "direct read", op_direct_read },
  { "direct write", op_direct_write },
  { "mmapped read", op_mmapped_read },
  { "mmapped write", op_mmapped_write },
  { "truncate", op_trunc },
  { "nop", op_nop },
};


int do_op()
{
  filecon2_request_t req;
  unsigned char buf[sizeof(filecon2_request_t)];
  float c, x = 0.0;
  unsigned int op;


  do_read(sock, buf, sizeof(filecon2_request_t));
  request_in(&req, buf);

  if (req.magic != FILECON2_MAGIC)
    die("magic number mismatch\n");
  if (req.type == FCR_STOP)
    return FALSE;
  if (req.type)
    die("strange message from server\n");

  file_size = req.offset;
  opnum = req.length;


  if (first)
  {
    do_first();
    return TRUE;
  }
  else if (check_file_size)
    do_check_file_size();


  for (;;)
  {
    op = 0;
    c = RAND(1);

    for (;;)
    {
      if (chances[op])
      {
	x += chances[op];
	if (x > c)
	{
	  ops[op].func();
	  return TRUE;
	}
      }

      op++;
      if (op == CHANCES)
	break;
    }
  }


  die("D'oh!\n");
}


void compute_chances()
{
  unsigned int x;
  float sum = 0.0;

  for (x = 0; x < CHANCES; x++)
    chances[x] = 0.0;

  if (buffered)
  {
    chances[0] = 1.0;
    chances[1] = 1.0;
  }
  if (direct)
  {
    chances[2] = 1.0;
    chances[3] = 1.0;
  }
  if (mmapped)
  {
    chances[4] = 1.0;
    chances[5] = 1.0;
  }
  if (trunc)
    chances[6] = 0.1;
  if (nop)
    chances[7] = 0.1;

  for (x = 0; x < CHANCES; x++)
    sum += chances[x];

  for (x = 0; x < CHANCES; x++)
    chances[x] /= sum;
}


void do_seed()
{
  filecon2_request_t req;
  unsigned char buf[sizeof(filecon2_request_t)];

  do_read(sock, buf, sizeof(filecon2_request_t));
  request_in(&req, buf);

  if (req.magic != FILECON2_MAGIC)
    die("magic number mismatch\n");
  if (req.type != FCR_SEED)
    die("strange message from server\n");

  if (!user_seed)
    seed = req.length;

  if (verbose)
    printf("\nseed = %u\n", seed);

  srandom(seed);
}


int main(int argc, char *argv[])
{
  int optchar, cont = TRUE;
  struct hostent *hname;
  struct sockaddr_in sin;
  unsigned int x;
  int error;


  prog_name = argv[0];
  pid = getpid();


  if (argc < 3)
  {
    fprintf(stderr, "%s usage:\n\n", prog_name);
    fprintf(stderr, "%s -p <port> -s <seed> -o <offset> -l <length> -c <chunksize> -f <pattern> -b -d -m -t -n -a <align> -r -e -x -v server file\n\n", prog_name);
    fprintf(stderr, "  -p <port>          Port to connect to\n");
    fprintf(stderr, "  -s <seed>          Seed for the random number generator\n");
    fprintf(stderr, "  -o <offset>        The start of the active region in bytes\n");
    fprintf(stderr, "  -l <length>        The length of the active region in bytes\n");
    fprintf(stderr, "  -c <chunksize>     Read/Write up to this amount in one I/O\n");
    fprintf(stderr, "  -f <pattern>       Do writes with this pattern (rand, opnum)\n");
    fprintf(stderr, "  -b                 Do buffered I/O\n");
    fprintf(stderr, "  -d                 Do direct I/O\n");
    fprintf(stderr, "  -m                 Do memory mapped I/O\n");
    fprintf(stderr, "  -t                 Do truncates\n");
    fprintf(stderr, "  -n                 Do nops\n");
    fprintf(stderr, "  -a <align>         I/O should be aligned to multiples of this value\n");
    fprintf(stderr, "  -r                 Reread writes\n");
    fprintf(stderr, "  -e                 Don't try to read past the EOF when doing Direct I/O\n");
    fprintf(stderr, "  -x                 Always check to make sure the file's size is correct\n");
    fprintf(stderr, "  -v                 Be verbose\n");
    exit(EXIT_FAILURE);
  }

  while (cont)
  {
    optchar = getopt(argc, argv, "p:s:o:l:c:f:bdmtna:rexv");
    switch (optchar)
    {
    case 'p':
      sscanf(optarg, "%u", &port);
      break;
    case 's':
      sscanf(optarg, "%u", &seed);
      user_seed = TRUE;
      break;
    case 'o':
      sscanf(optarg, "%"SCNu64"", &offset);
      break;
    case 'l':
      sscanf(optarg, "%"SCNu64"", &length);
      break;
    case 'c':
      sscanf(optarg, "%u", &chunk);
      break;
    case 'f':
      if (strcmp(optarg, "rand") == 0)
	pattern = PATTERN_RAND;
      else if (strcmp(optarg, "opnum") == 0)
	pattern = PATTERN_OPNUM;
      else
	die("unknown pattern %s\n", optarg);
      break;
    case 'b':
      buffered = TRUE;
      break;
    case 'd':
      direct = TRUE;
      break;
    case 'm':
      mmapped = TRUE;
      break;
    case 't':
      trunc = TRUE;
      break;
    case 'n':
      nop = TRUE;
      break;
    case 'a':
      sscanf(optarg, "%u", &align);
      break;
    case 'r':
      reread = TRUE;
      break;
    case 'e':
      respect_eof = TRUE;
      break;
    case 'x':
      check_file_size = TRUE;
      break;
    case 'v':
      verbose = TRUE;
      break;
    case EOF:
      cont = FALSE;
      break;
    default:
      die("bad argument\n");
    }
  }

  if (optind < argc)
    servername = argv[optind++];
  else
    die("no servername\n");

  if (optind < argc)
    filename = argv[optind++];
  else
    die("no filename\n");

  if (port >= 65536)
    die("invalid port number: %u\n", port);

  if (align > chunk || chunk > length)
    die("bad sizes, should be: -a <= -c < = -l\n");

  if (offset % align)
    die("offset not aligned\n");

  if (mmapped)
  {
    if (trunc)
      die("can't do both -m and -t\n");
    first = TRUE;
  }


  compute_chances();


  if (verbose)
  {
    printf("port = %u\n", port);
    if (user_seed)
      printf("seed = %u\n", seed);
    else
      printf("seed = <ProvidedByServer>\n");
    printf("offset = %"PRIu64"\n", offset);
    printf("length = %"PRIu64"\n", length);
    printf("chunk = %u\n", chunk);
    switch (pattern)
    {
    case PATTERN_RAND:
      printf("pattern = rand\n");
      break;
    case PATTERN_OPNUM:
      printf("pattern = opnum\n");
      break;
    default:
      printf("pattern = unknown\n");
      break;
    }
    printf("buffered = %d\n", buffered);
    printf("direct = %d\n", direct);
    printf("mmapped = %d\n", mmapped);  
    printf("trunc = %d\n", trunc);
    printf("nop = %d\n", nop);
    printf("align = %u\n", align);
    printf("reread = %d\n", reread);
    printf("respect_eof = %d\n", respect_eof);
    printf("check_file_size = %d\n", check_file_size);
    printf("verbose = %d\n", verbose);
    printf("servername = %s\n", servername);
    printf("filename = %s\n", filename);

    printf("\n");
    for (x = 0; x < CHANCES; x++)
      printf("%s %f\n", ops[x].name, chances[x]);
  }


  fd_b = open(filename, O_RDWR);
  if (fd_b < 0)
    die("can't open file %s: %s\n", filename, strerror(errno));

  if (direct)
  {
    fd_d = open(filename, O_RDWR | O_DIRECT);
    if (fd_d < 0)
      die("can't open file %s: %s\n", filename, strerror(errno));
  }


  hname = gethostbyname(servername);
  if (!hname)
    die("can't resolve host %s: %s\n", servername, strerror(errno));

  sock = socket(AF_INET, SOCK_STREAM, 0);
  if (sock < 0)
    die("can't open socket: %s\n", strerror(errno));

  sin.sin_family = AF_INET;
  sin.sin_addr.s_addr = *((uint32 *)*(hname->h_addr_list));
  sin.sin_port = cpu_to_be16(port);
 
  error = connect(sock, (struct sockaddr *)&sin, sizeof(struct sockaddr_in));
  if (error < 0)
    die("can't connect to host %s (%u): %s\n", servername, port, strerror(errno));


  do_seed();


  while (do_op()) /* Do nothing */;


  close(sock);
  if (direct)
    close(fd_d);
  if (mmap_data)
    munmap(mmap_data, length);
  close(fd_b);


  exit(EXIT_SUCCESS);
}



