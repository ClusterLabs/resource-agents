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
#include "osi_list.h"
#include "linux_endian.h"
#define EXTERN
#include "filecon2.h"



struct log
{
  osi_list_t list;
  unsigned int cli;
  filecon2_request_t req;
};
typedef struct log log_t;



int fd = -1;
unsigned int clients = 0;
struct sockaddr_in *ssin = NULL;
int *sock = NULL;
unsigned int t = 0;

int log = FALSE;
osi_list_decl(log_list);





char *type2string(unsigned int type)
{
  char *type_string;

  switch (type)
  {
  case FCR_READ_BUFFERED:
    type_string = "read(b)";
    break;
  case FCR_READ_DIRECT:
    type_string = "read(d)";
    break;
  case FCR_READ_MMAPPED:
    type_string = "read(m)";
    break;
  case FCR_WRITE_BUFFERED:
    type_string = "write(b)";
    break;
  case FCR_WRITE_DIRECT:
    type_string = "write(d)";
    break;
  case FCR_WRITE_MMAPPED:
    type_string = "write(m)";
    break;
  case FCR_TRUNC:
    type_string = "trunc";
    break;
  case FCR_NOP:
    type_string = "nop";
    break;
  case FCR_SEED:
    type_string = "seed";
    break;
  case FCR_STOP:
    type_string = "stop";
    break;
  default:
    type_string = "unknown";
    break;
  }

  return type_string;
}


void request_print(unsigned int cli, filecon2_request_t *req)
{
  printf("Request from %u/%.8X/%.4X:\n",
	 cli, be32_to_cpu(ssin[cli].sin_addr.s_addr), be16_to_cpu(ssin[cli].sin_port));
  printf("  magic = 0x%X\n", req->magic);
  printf("  type = %s (%u)\n", type2string(req->type), req->type);
  printf("  length = %u\n", req->length);
  printf("  data = %u\n", req->data);
  printf("  offset = %"PRIu64"\n", req->offset);
}


void dump_data(filecon2_request_t *req, unsigned char *client, unsigned char *server)
{
  FILE *outfile;
  unsigned int x;
  unsigned int first = (unsigned int)-1, last = (unsigned int)-1;
  uint64 f1, l1, f2, l2;

  for (x = 0; x < req->data; x++)
    if (client[x] != server[x])
    {
      first = x;
      break;
    }

  if (first == (unsigned int)-1)
    die("blerg1\n");

  for (x = req->data; x--; )
    if (client[x] != server[x])
    {
      last = x;
      break;
    }

  if (last == (unsigned int)-1)
    die("blerg2\n");

  f1 = req->offset + first;
  l1 = req->offset + last;

  printf("Error range (%"PRIu64" - %"PRIu64"), (%u - %u) \n",
	 f1, l1, first, last);

  if (log)
  {
    osi_list_t *tmp, *head;
    log_t *l;
    unsigned int t = 0;

    for (head = &log_list, tmp = head->next;
	 tmp != head;
	 tmp = tmp->next, t++)
    {
      l = osi_list_entry(tmp, log_t, list);

      if (l->req.type == FCR_NOP)
	continue;
      else if (l->req.type == FCR_TRUNC)
      {
	f2 = l->req.offset;
	l2 = l->req.offset;
      }
      else
      {
	f2 = l->req.offset;
	l2 = l->req.offset + l->req.data - 1;
      }

      if ((f1 <= f2 && f2 <= l1) ||
	  (f1 <= l2 && l2 <= l1) ||
	  (f2 <= f1 && f1 <= l2) ||
	  (f2 <= l1 && l1 <= l2))
      {
	printf("%.7u: %u/%.8X/%.4X: %s (%"PRIu64" - %"PRIu64")\n",
	       t,
	       l->cli, be32_to_cpu(ssin[l->cli].sin_addr.s_addr), be16_to_cpu(ssin[l->cli].sin_port),
	       type2string(l->req.type),
	       f2, l2);
      }
    }
  }

  outfile = fopen("out.client", "w");
  if (!outfile)
    die("can't open file %s: %s\n", "out.client", strerror(errno));
  for (x = 0; x < req->data; x++)
  {
    fprintf(outfile, "%.2X", client[x]);
    if (x % 32 == 31)
      fprintf(outfile, "\n");
  }
  if (x % 32)
    fprintf(outfile, "\n");
  fclose(outfile);

  outfile = fopen("out.server", "w");
  if (!outfile)
    die("can't open file %s: %s\n", "out.server", strerror(errno));
  for (x = 0; x < req->data; x++)
  {
    fprintf(outfile, "%.2X", server[x]);
    if (x % 32 == 31)
      fprintf(outfile, "\n");
  }
  if (x % 32)
    fprintf(outfile, "\n");
  fclose(outfile);
}


void do_op(unsigned int cli)
{
  struct stat st;
  filecon2_request_t req;
  unsigned char buf[sizeof(filecon2_request_t)];
  uint64 first, last;
  unsigned char *data1 = NULL;
  unsigned char *data2;
  unsigned int x = 0;
  int error;

  error = fstat(fd, &st);
  if (error)
    die("can't stat file: %s\n", strerror(errno));

  memset(&req, 0, sizeof(filecon2_request_t));
  req.magic = FILECON2_MAGIC;
  req.length = t;
  req.offset = st.st_size;
  request_out(&req, buf);
  do_write(sock[cli], buf, sizeof(filecon2_request_t));

  do_read(sock[cli], buf, sizeof(filecon2_request_t));
  request_in(&req, buf);

  if (req.magic != FILECON2_MAGIC)
  {
    request_print(cli, &req);
    die("magic number mismatch\n");
  }

  if (req.type == FCR_NOP)
    first = last = 0;
  else if (req.type == FCR_TRUNC)
    first = last = req.offset;
  else
  {
    first = req.offset;
    last = req.offset + req.data - 1;
  }

  printf("%.7u: %d/%u/%.8X/%.4X: %s (%"PRIu64" - %"PRIu64")\n",
	 t, pid,
	 cli, be32_to_cpu(ssin[cli].sin_addr.s_addr), be16_to_cpu(ssin[cli].sin_port),
	 type2string(req.type),
	 first, last);

  if (log)
  {
    log_t *l;
    l = malloc(sizeof(log_t));
    if (!l)
      die("out of memory\n");
    memset(l, 0, sizeof(log_t));
    l->cli = cli;
    l->req = req;
    osi_list_add_prev(&l->list, &log_list);
  }

  if (req.data)
  {
    data1 = malloc(req.data);
    if (!data1)
    {
      request_print(cli, &req);
      die("out of memory\n");
    }

    for (;;)
    {
      error = read(sock[cli], data1 + x, req.data - x);
      if (error < 0)
      {
	request_print(cli, &req);
	die("can't read from socket: %s\n", strerror(errno));
      }

      x += error;

      if (x == req.data)
	break;
      if (x > req.data)
      {
	request_print(cli, &req);
	die("data overflow from client: %u\n", x);
      }
    }
  }

  if (req.type == FCR_READ_BUFFERED ||
      req.type == FCR_READ_MMAPPED)
  {
    data2 = malloc(req.data + 1);  /*  req.data might be zero  */;
    if (!data2)
    {
      request_print(cli, &req);
      die("out of memory\n");
    }

    do_lseek(fd, req.offset);
    error = read(fd, data2, req.length);
    if (error < 0)
    {
      request_print(cli, &req);
      die("bad read from file: %s\n", strerror(errno));
    }

    if (error != req.data)
    {
      request_print(cli, &req);
      die("length mismatch from read: %u\n", error);
    }
    if (req.data && memcmp(data1, data2, req.data) != 0)
    {
      request_print(cli, &req);
      dump_data(&req, data1, data2);
      die("value mismatch\n");
    }

    free(data2);
  }
  else if (req.type == FCR_READ_DIRECT)
  {
    data2 = malloc(req.length + 1);
    if (!data2)
    {
      request_print(cli, &req);
      die("out of memory\n");
    }

    do_lseek(fd, req.offset);
    error = read(fd, data2, req.length);
    if (error < 0)
    {
      request_print(cli, &req);
      die("bad read from file: %s\n", strerror(errno));
    }

    /*  FixMe!!!  */
    if (error < req.data || error - req.data > 4096)
    {
      request_print(cli, &req);
      die("length mismatch from read: %u\n", error);
    }
    if (req.data && memcmp(data1, data2, req.data) != 0)
    {
      request_print(cli, &req);
      dump_data(&req, data1, data2);
      die("value mismatch\n");
    }

    free(data2);
  }
  else if (req.type == FCR_WRITE_BUFFERED ||
	   req.type == FCR_WRITE_DIRECT ||
	   req.type == FCR_WRITE_MMAPPED)
  {
    if (!req.data)
    {
      request_print(cli, &req);
      die("no data to write\n");
    }
    if (req.length != req.data)
    {
      request_print(cli, &req);
      die("length != data on write\n");
    }

    do_lseek(fd, req.offset);
    error = write(fd, data1, req.length);
    if (error < 0)
    {
      request_print(cli, &req);
      die("bad write to file: %s\n", strerror(errno));
    }
    if (error != req.length)
    {
      request_print(cli, &req);
      die("short write: %u\n", error);
    }
  }
  else if (req.type == FCR_TRUNC)
  {
    error = ftruncate(fd, req.offset);
    if (error)
    {
      request_print(cli, &req);
      die("bad truncate: %s\n", strerror(errno));
    }
  }
  else if (req.type == FCR_NOP)
  {
    /*  Do nothing  */
  }
  else
  {
    request_print(cli, &req);
    die("strange message from client\n");
  }

  if (req.data)
    free(data1);
}


void do_seed(int sock)
{
  filecon2_request_t req;
  unsigned char buf[sizeof(filecon2_request_t)];

  memset(&req, 0, sizeof(filecon2_request_t));
  req.magic = FILECON2_MAGIC;
  req.type = FCR_SEED;
  req.length = random();
  request_out(&req, buf);
  do_write(sock, buf, sizeof(filecon2_request_t));
}


void do_stop(int sock)
{
  filecon2_request_t req;
  unsigned char buf[sizeof(filecon2_request_t)];

  memset(&req, 0, sizeof(filecon2_request_t));
  req.magic = FILECON2_MAGIC;
  req.type = FCR_STOP;
  request_out(&req, buf);
  do_write(sock, buf, sizeof(filecon2_request_t));
}


int main(int argc, char *argv[])
{
  unsigned int port = FILECON2_PORT;
  unsigned int seed = time(NULL) ^ getpid();
  unsigned int wait_clients = 1;
  unsigned int to = 0;
  char *filename;

  int optchar, cont = TRUE;
  int asking_sock;
  int trueint = TRUE;
  struct sockaddr_in sin;
  fd_set fds;
  struct timeval tv;
  unsigned int size;
  unsigned int cli;
  int error;


  prog_name = argv[0];
  pid = getpid();


  if (argc < 2)
  {
    fprintf(stderr, "%s usage:\n\n", prog_name);
    fprintf(stderr, "%s -p <port> -s <seed> -w <clients> -t <to> filename\n\n", prog_name);
    fprintf(stderr, "  -p <port>          The port to listen on (%u by default)\n", FILECON2_PORT);
    fprintf(stderr, "  -s <seed>          Seed for the random number generator\n");
    fprintf(stderr, "  -w <clients>       Wait for a number of clients to join\n");
    fprintf(stderr, "  -t <to>            Only do a given number of operations\n");
    fprintf(stderr, "  -l                 Log operations for error reporting\n");
    exit(EXIT_FAILURE);
  }

  while (cont)
  {
    optchar = getopt(argc, argv, "p:s:w:t:l");
    switch (optchar)
    {
    case 'p':
      sscanf(optarg, "%u", &port);
      break;
    case 's':
      sscanf(optarg, "%u", &seed);
      break;
    case 'w':
      sscanf(optarg, "%u", &wait_clients);
      break;
    case 't':
      sscanf(optarg, "%u", &to);
      break;
    case 'l':
      log = TRUE;
      break;
    case EOF:
      cont = FALSE;
      break;
    default:
      die("bad argument\n");
    }
  }

  if (optind < argc)
    filename = argv[optind];
  else
    die("no filename\n");

  if (port >= 65536)
    die("invalid port number: %u\n", port);


  srandom(seed);


  fd = open(filename, O_RDWR, 0644);
  if (fd < 0)
    die("can't open file %s: %s\n", filename, strerror(errno));


  asking_sock = socket(AF_INET, SOCK_STREAM, 0);
  if (asking_sock < 0)
    die("can't open socket: %s\n", strerror(errno));

  error = setsockopt(asking_sock, SOL_SOCKET, SO_REUSEADDR, &trueint, sizeof(int));
  if (error < 0)
    die("can't set SO_REUSEADDR: %s\n", strerror(errno));

  sin.sin_family = AF_INET;
  sin.sin_addr.s_addr = INADDR_ANY;
  sin.sin_port = cpu_to_be16(port);

  error = bind(asking_sock, (struct sockaddr *)&sin, sizeof(struct sockaddr_in));
  if (error < 0)
    die("can't bind to port %u: %s\n", port, strerror(errno));

  error = listen(asking_sock, 5);
  if (error < 0)
    die("can't bind to socket: %s\n", strerror(errno));

  
  for (;;)
  {
    FD_ZERO(&fds);
    FD_SET(asking_sock, &fds);
    tv.tv_sec = 0;
    tv.tv_usec = 0;

    error = select(asking_sock + 1, &fds, NULL, NULL,
		   (clients < wait_clients) ? NULL : &tv);
    if (error < 0)
      die("can't select: %s\n", strerror(errno));

    if (error)  /*  Add a new client  */
    {
      cli = clients;
      clients++;

      sock = realloc(sock, clients * sizeof(int));
      if (!sock)
	die("can't alloc memory: %s\n", strerror(errno));
      ssin = realloc(ssin, clients * sizeof(struct sockaddr_in));
      if (!sock)
	die("can't alloc memory: %s\n", strerror(errno));

      size = sizeof(struct sockaddr_in);
      sock[cli] = accept(asking_sock, &ssin[cli], &size);
      if (sock[cli] < 0)
	die("can't accept: %s\n", strerror(errno));

      do_seed(sock[cli]);

      printf("connect %u/%.8X/%.4X\n",
	     cli, be32_to_cpu(ssin[cli].sin_addr.s_addr), be16_to_cpu(ssin[cli].sin_port));
    }

    if (clients < wait_clients)
      continue;


    cli = RAND(clients);
    do_op(cli);
    t++;

    if (to && t == to)
      break;
  }


  for (cli = 0; cli < clients; cli++)
  {
    do_stop(sock[cli]);
    close(sock[cli]);
  }
  close(fd);


  exit(EXIT_SUCCESS);
}



