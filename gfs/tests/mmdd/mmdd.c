#ifdef __linux__
#else
#undef USE_SENDFILE
#endif

#ifdef __FreeBSD__
#define O_SYNC O_FSYNC
#else
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <sys/time.h>
#include <sys/mman.h>
#ifdef USE_SENDFILE
#include <sys/sendfile.h>
#endif



#define die(fmt, args...) \
do \
{ \
  fprintf(stderr, "%s: ", prog_name); \
  fprintf(stderr, fmt, ##args); \
  exit(EXIT_FAILURE); \
} \
while (0)

#define TRUE (1)
#define FALSE (0)



#ifndef O_DIRECT
#define O_DIRECT (0)
#warning O_DIRECT is broken
#endif
#ifndef PAGE_SIZE
#define PAGE_SIZE (65536)
#endif



char *prog_name;
off_t read_offset = 0;
off_t write_offset = 0;
int private = FALSE;





#ifdef USE_SENDFILE
void send_sendfile(int ifd, int ofd, unsigned bs)
{
  int count;

  count = sendfile(ofd, ifd, &read_offset, bs);
  if (count != bs)
    die("Bad sendfile return %d: %s\n", count, strerror(errno));   

  write_offset += bs;
}
#endif


void do_bothmapped(int ifd, int ofd, unsigned bs)
{
  char *data_in;
  char *data_out;

  data_in = mmap(NULL, bs, PROT_READ, (private) ? MAP_PRIVATE : MAP_SHARED, ifd, read_offset);
  if (data_in == MAP_FAILED)
    die("can't mmap file: %s\n", strerror(errno));

  data_out = mmap(NULL, bs, PROT_READ | PROT_WRITE, (private) ? MAP_PRIVATE : MAP_SHARED, ofd, write_offset);
  if (data_out == MAP_FAILED)
    die("can't mmap file: %s\n", strerror(errno));
  
  memcpy(data_out, data_in, bs);

  munmap(data_in, bs);
  munmap(data_out, bs);

  read_offset += bs;
  write_offset += bs;
}


void do_imapped(int ifd, int ofd, unsigned bs)
{
  char *data;
  int count;

  data = mmap(NULL, bs, PROT_READ, (private) ? MAP_PRIVATE : MAP_SHARED, ifd, read_offset);
  if (data == MAP_FAILED)
    die("can't mmap file: %s\n", strerror(errno));

  count = write(ofd, data, bs);
  if (count != bs)
    die("bad write return %d: %s\n", count, strerror(errno));

  munmap(data, bs);

  read_offset += bs;
  write_offset += bs;
}


void do_omapped(int ifd, int ofd, unsigned bs)
{
  char *data;
  int count;

  data = mmap(NULL, bs, PROT_READ | PROT_WRITE, (private) ? MAP_PRIVATE : MAP_SHARED, ofd, write_offset);
  if (data == MAP_FAILED)
    die("can't mmap file: %s\n", strerror(errno));

  count = read(ifd, data, bs);
  if (count != bs)
    die("bad read return %d: %s\n", count, strerror(errno));
  
  munmap(data, bs);

  read_offset += bs;
  write_offset += bs;
}


void do_mapped_read(int fd, char *buf, unsigned int bs)
{
  char *data;

  data = mmap(NULL, bs, PROT_READ, (private) ? MAP_PRIVATE : MAP_SHARED, fd, read_offset);
  if (data == MAP_FAILED)
    die("can't mmap file: %s\n", strerror(errno));

  memcpy(buf, data, bs);

  munmap(data, bs);

  read_offset += bs;
}


void do_raw_read(int fd, char *buf, unsigned int bs)
{
  int count;

  count = read(fd, buf, bs);
  if (count != bs)
    die("bad read return %d: %s\n", count, strerror(errno));
}


void do_mapped_write(int fd, char *buf, unsigned int bs)
{
  char *data;

  data = mmap(NULL, bs, PROT_READ | PROT_WRITE, (private) ? MAP_PRIVATE : MAP_SHARED, fd, write_offset);
  if (data == MAP_FAILED)
    die("can't mmap file: %s\n", strerror(errno));

  memcpy(data, buf, bs);

  munmap(data, bs);

  write_offset += bs;
}


void do_raw_write(int fd, char *buf, unsigned int bs)
{
  int count;

  count = write(fd, buf, bs);
  if (count != bs)
    die("bad write return %d: %s\n", count, strerror(errno));
}


int main(int argc, char *argv[])
{
  int arg;

  char *ifname = NULL;
  char *ofname = NULL;
  int imapped = FALSE, omapped = FALSE;
  unsigned int bs = 4096;
  unsigned long long move = 0;
  unsigned long long count = 0;
  unsigned long long skip = 0;
  unsigned long long seek = 0;
  int do_fsync = FALSE;
  int do_osync = FALSE;
  int verbose = FALSE;
  int notrunc = FALSE;
  int nobounce = FALSE;
  int append = FALSE;
  int direct = FALSE;
  int fgt = FALSE;
  int do_excl = FALSE;
  
  int done_bs = FALSE;
  int done_amount = FALSE;
  int done_skip = FALSE;
  int done_seek = FALSE;
  int done_fsync = FALSE;
  int done_osync = FALSE;
  int done_notrunc = FALSE;
  int done_nobounce = FALSE;
  int done_append = FALSE;
  int done_direct = FALSE;
  int done_private = FALSE;
  int done_fgt = FALSE;
  int done_excl = FALSE;

#ifdef USE_SENDFILE
  int do_sendfile = FALSE;
  int done_sendfile = FALSE;
#endif
  
  int ifd = -1, ofd = -1;

  char c;
  char *data;
  unsigned long long x;
  unsigned int remainder;

  unsigned int timeslots, index = 0;
  struct timeval *tv;
  unsigned long long t1, t2;
  double seconds, mbps;

  struct stat st;
  char *fgt_name = NULL;
  FILE *fgt_file;

  int error;



  prog_name = argv[0];



  if (argc == 1)
    die("options:  Help me!  Help me!\n");



  for (arg = 1; arg < argc; arg++)
  {
    if (strncmp(argv[arg], "if=", 3) == 0)
    {
      if (ifname)
	die("you're only allowed one \"i?f=\"\n");

      ifname = argv[arg] + 3;
      imapped = FALSE;
    }
    else if (strncmp(argv[arg], "imf=", 4) == 0)
    {
      if (ifname)
	die("you're only allowed one \"i?f=\"\n");

      ifname = argv[arg] + 4;
      imapped = TRUE;
    }
    else if (strncmp(argv[arg], "of=", 3) == 0)
    {
      if (ofname)
	die("you're only allowed one \"o?f=\"\n");

      ofname = argv[arg] + 3;
      omapped = FALSE;
    }
    else if (strncmp(argv[arg], "omf=", 4) == 0)
    {
      if (ofname)
	die("you're only allowed one \"o?f=\"\n");

      ofname = argv[arg] + 4;
      omapped = TRUE;
    }
    else if (strncmp(argv[arg], "bs=", 3) == 0)
    {
      if (done_bs)
	die("you're only allowed one \"bs=\"\n");
      done_bs = TRUE;

      bs = atoi(argv[arg] + 3);
      c = argv[arg][strlen(argv[arg]) - 1];
      if (c == 'k')
	bs <<= 10;
      else if (c == 'm')
	bs <<= 20;
      else if (c == 'g')
	bs <<= 30;

      if (!bs)
	die("invalid blocksize: %u\n", bs);
    }
    else if (strncmp(argv[arg], "move=", 5) == 0)
    {
      if (done_amount)
	die("you're only allowed one amount\n");
      done_amount = TRUE;

      sscanf(argv[arg] + 5, "%qu", &move);
      c = argv[arg][strlen(argv[arg]) - 1];
      if (c == 'k')
	move <<= 10;
      else if (c == 'm')
	move <<= 20;
      else if (c == 'g')
	move <<= 30;

      if (!move)
	die("zero move\n");
    }
    else if (strncmp(argv[arg], "count=", 6) == 0)
    {
      if (done_amount)
	die("you're only allowed one amount\n");
      done_amount = TRUE;

      sscanf(argv[arg] + 6, "%qu", &count);

      if (!count)
	die("zero count\n");
    }
    else if (strncmp(argv[arg], "skip=", 5) == 0)
    {
      if (done_skip)
	die("you're only allowed one skip\n");
      done_skip = TRUE;

      sscanf(argv[arg] + 5, "%qu", &skip);
    }
    else if (strncmp(argv[arg], "seek=", 5) == 0)
    {
      if (done_seek)
	die("you're only allowed one seek\n");
      done_seek = TRUE;

      sscanf(argv[arg] + 5, "%qu", &seek);
    }
    else if (strncmp(argv[arg], "fsync=", 6) == 0)
    {
      if (done_fsync)
	die("you're only allowed one \"fsync=\"\n");
      done_fsync = TRUE;

      do_fsync = atoi(argv[arg] + 6);
    }
    else if (strncmp(argv[arg], "osync=", 6) == 0)
    {
      if (done_osync)
	die("you're only allowed one \"osync=\"\n");
      done_osync = TRUE;

      do_osync = atoi(argv[arg] + 6);
    }
    else if (strncmp(argv[arg], "notrunc=", 8) == 0)
    {
      if (done_notrunc)
	die("you're only allowed one \"notrunc=\"\n");
      done_notrunc = TRUE;

      notrunc = atoi(argv[arg] + 8);
    }
    else if (strncmp(argv[arg], "nobounce=", 9) == 0)
    {
      if (done_nobounce)
	die("you're only allowed one \"nobounce=\"\n");
      done_nobounce = TRUE;

      nobounce = atoi(argv[arg] + 9);
    }
    else if (strncmp(argv[arg], "append=", 7) == 0)
    {
      if (done_append)
	die("you're only allowed one \"append=\"\n");
      done_append = TRUE;

      append = atoi(argv[arg] + 7);
    }
    else if (strncmp(argv[arg], "direct=", 7) == 0)
    {
      if (done_direct)
	die("you're only allowed one \"direct=\"\n");
      done_direct = TRUE;

      direct = atoi(argv[arg] + 7);
    }
#ifdef USE_SENDFILE
    else if (strncmp(argv[arg], "sendfile=", 9) == 0)
    {
      if (done_sendfile)
	die("you're only allowed one \"sendfile=\"\n");
      done_sendfile = TRUE;

      do_sendfile = atoi(argv[arg] + 9);
    }
#endif
    else if (strncmp(argv[arg], "private=", 8) == 0)
    {
      if (done_private)
	die("you're only allowed one \"private=\"\n");
      done_private = TRUE;

      private = atoi(argv[arg] + 8);
    }
    else if (strncmp(argv[arg], "fgt=", 4) == 0)
    {
      if (done_fgt)
	die("you're only allowed one \"fgt=\"\n");
      done_fgt = TRUE;

      if (isdigit(*(argv[arg] + 4)))
	fgt = atoi(argv[arg] + 4);
      else
      {
	fgt = TRUE;
	fgt_name = argv[arg] + 4;
      }
    }
    else if (strncmp(argv[arg], "excl=", 5) == 0)
    {
      if (done_excl)
	die("you're only allowed one \"excl=\"\n");
      done_excl = TRUE;

      do_excl = atoi(argv[arg] + 5);
    }
    else if (strcmp(argv[arg], "-v") == 0)
    {
      verbose = TRUE;
    }
    else
    {
      die("unknown/unimplemented option: %s\n", argv[arg]);
    }
  }



  if (!done_amount)
    die("need an amount to move\n");

  if (verbose)
  {
    fprintf(stderr, "ifname = %s (%s)\n", ifname, (imapped) ? "mapped" : "raw");
    fprintf(stderr, "ofname = %s (%s)\n", ofname, (omapped) ? "mapped" : "raw");
    fprintf(stderr, "bs = %u\n", bs);
    fprintf(stderr, "move = %qu\n", move);
    fprintf(stderr, "count = %qu\n", count);
    fprintf(stderr, "skip = %qu\n", skip);
    fprintf(stderr, "seek = %qu\n", seek);
    fprintf(stderr, "fsync = %d\n", do_fsync);
    fprintf(stderr, "notrunc = %d\n", notrunc);
    fprintf(stderr, "nobounce = %d\n", nobounce);
    fprintf(stderr, "append = %d\n", append);
    fprintf(stderr, "direct = %d\n", direct);
#ifdef USE_SENDFILE
    fprintf(stderr, "sendfile = %d\n", do_sendfile);
#endif
    fprintf(stderr, "private = %d\n", private);
    fprintf(stderr, "fine grain timing = %d\n", fgt);
    fprintf(stderr, "fine grain timing file = %s\n", (fgt_name) ? fgt_name : "NULL");
    fprintf(stderr, "excl = %d\n", do_excl);
  }

  if (!count)
  {
    if (move % bs != 0)
      die("move size (%qu) not divisible by block size (%u)\n", move, bs);
    count = move / bs;
  }



  data = (char *)malloc(bs + PAGE_SIZE);
  if (!data)
    die("out of memory\n");

  remainder = ((unsigned long)data) & (PAGE_SIZE - 1);
  if (remainder)
    data += PAGE_SIZE - remainder;

  memset(data, 0, bs);



  if (fgt)
  {
    if (count & 0xFFFFFFFFF0000000)
      die("count is too high -- can't do fine grain timing\n");
    timeslots = count + 3;
  }
  else
    timeslots = 2;

  tv = malloc(timeslots * sizeof(struct timeval));
  if (!tv)
    die("out of memory\n");



  gettimeofday(&tv[index++], NULL);



  if (ifname)
  {
    if (strcmp(ifname, "-") == 0)
      ifd = STDIN_FILENO;
    else
    {
      ifd = open(ifname, O_RDONLY | ((direct) ? O_DIRECT : 0));
      if (ifd < 0)
	die("can't open file %s: %s\n", ifname, strerror(errno));
      if (verbose)
      {
	error = fstat(ifd, &st);
	if (error)
	  die("can't stat file %s: %s\n", ifname, strerror(errno));
	fprintf(stderr, "input inode = %qu\n", (unsigned long long)st.st_ino);
      }

      if (skip && lseek(ifd, skip * bs, SEEK_SET) != skip * bs)
	die("can't skip:  %s\n", strerror(errno));
    }
  }

  if (ofname)
  {
    if (strcmp(ofname, "-") == 0)
      ofd = STDOUT_FILENO;
    else
    {
      ofd = open(ofname,
		 O_RDWR |
		 O_CREAT |
		 ((notrunc) ? 0 : O_TRUNC) |
		 ((direct) ? O_DIRECT : 0) |
		 ((append) ? O_APPEND : 0) |
		 ((do_osync) ? O_SYNC : 0) |
		 ((do_excl) ? O_EXCL : 0),
		 0644);
      if (ofd < 0)
	die("can't open file %s: %s\n", ofname, strerror(errno));
      if (verbose)
      {
	error = fstat(ofd, &st);
	if (error)
	  die("can't stat file %s: %s\n", ofname, strerror(errno));
	fprintf(stderr, "output inode = %qu\n", (unsigned long long)st.st_ino);
      }

      if (seek && lseek(ofd, seek * bs, SEEK_SET) != seek * bs)
	die("can't seek:  %s\n", strerror(errno));
    }
  }

  if (ofd >= 0 && omapped)
  {
    if (ftruncate(ofd, bs * count) < 0)
      die("can't truncate bigger: %s\n", strerror(errno));
  }



  if (fgt)
    gettimeofday(&tv[index++], NULL);



  for (x = 0; x < count; x++)
  {
#ifdef USE_SENDFILE
    if (do_sendfile && ifd >= 0 && ofd >= 0)
      send_sendfile(ifd, ofd, bs);
    else 
#endif
    if (nobounce && ifd >= 0 && ofd >= 0 && (imapped || omapped))
    {
      if (imapped && omapped)
	do_bothmapped(ifd, ofd, bs);
      else if (imapped)
	do_imapped(ifd, ofd, bs);
      else
	do_omapped(ifd, ofd, bs);
    }  
    else
    {
      if (ifd >= 0)
      {
	if (imapped)
	  do_mapped_read(ifd, data, bs);
	else
	  do_raw_read(ifd, data, bs);
      }

      if (ofd >= 0)
      {
	if (omapped)
	  do_mapped_write(ofd, data, bs);
	else
	  do_raw_write(ofd, data, bs);
      }
    }

    if (verbose)
      fprintf(stderr, "%qu\n", x);

    if (fgt)
      gettimeofday(&tv[index++], NULL);
  }



  if (ifd >= 0)
    close(ifd);

  if (ofd >= 0)
  {
    if (do_fsync)
      fsync(ofd);
    close(ofd);
  }



  gettimeofday(&tv[index++], NULL);

  if (index != timeslots)
    die("index != timeslots\n");



  if (fgt)
  {
    if (fgt_name)
    {
      fgt_file = fopen(fgt_name, "w");
      if (!fgt_file)
	die("can't open file %s:  %s\n", fgt_name, strerror(errno));
    }
    else
      fgt_file = stderr;

    t1 = (unsigned long long)tv[0].tv_sec * 1000000 + tv[0].tv_usec;
    fprintf(fgt_file, "start = %qu\n", t1);

    for (index = 1; index < timeslots; index++)
    {
      t1 = (unsigned long long)tv[index - 1].tv_sec * 1000000 + tv[index - 1].tv_usec;
      t2 = (unsigned long long)tv[index].tv_sec * 1000000 + tv[index].tv_usec;

      if (index == 1)
	fprintf(fgt_file, "setup = %qu\n", t2 - t1);
      else if (index == timeslots - 1)
	fprintf(fgt_file, "cleanup = %qu\n", t2 - t1);
      else
	fprintf(fgt_file, "loop %u = %qu\n", index - 2, t2 - t1);
    }

    t1 = (unsigned long long)tv[0].tv_sec * 1000000 + tv[0].tv_usec;
    t2 = (unsigned long long)tv[timeslots - 1].tv_sec * 1000000 + tv[timeslots - 1].tv_usec;

    fprintf(fgt_file, "total = %qu\n", t2 - t1);

    if (fgt_name)
      fclose(fgt_file);
  }



  t1 = (unsigned long long)tv[0].tv_sec * 1000000 + tv[0].tv_usec;
  t2 = (unsigned long long)tv[timeslots - 1].tv_sec * 1000000 + tv[timeslots - 1].tv_usec;

  seconds = (t2 - t1) / 1000000.0;
  mbps = bs * count / 1048576.0 / seconds;

  fprintf(stderr, "seconds = %.4f, MB/s = %.4f\n", seconds, mbps);



  exit(EXIT_SUCCESS);
}



