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

#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include <time.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <ctype.h>
#include <limits.h>
#include <sys/param.h>
#include <sys/mount.h>

#include "global.h"

#include <linux/gfs_ioctl.h>
#include <linux/gfs_ondisk.h>
typedef int osi_module_t;
#include <linux/lm_interface.h>

#include "copyright.cf"

#define EXTERN
#include "gfs_tool.h"



const char *usage[] =
{
#ifdef DEBUG_STACK
  "Print out stack traces:\n",
  "  gfs_tool stack <mountpoint>\n",
  "\n",
#endif
  "Print file stat data:\n",
  "  gfs_tool stat <filename>\n",
  "\n",
  "Print the superblock of a mounted filesystem:\n",
  "  gfs_tool getsb <mountpoint>\n",
  "\n",
  "Print the journal index of a mounted filesystem:\n",
  "  gfs_tool jindex <mountpoint>\n",
  "\n",
  "Print the resource group index of a mounted filesystem:\n",
  "  gfs_tool rindex <mountpoint>\n",
  "\n",
  "Print the quota file of a mounted filesystem:\n",
  "  gfs_tool quota <mountpoint>\n",
  "\n",
  "Print out the ondisk layout for a file:\n",
  "  gfs_tool layout <filename> [buffersize]\n",
  "\n",
  "Shrink a filesystem's inode cache:\n",
  "  gfs_tool shrink <mountpoint>\n",
  "\n",
  "Have GFS dump its lock state:\n",
  "  gfs_tool lockdump <mountpoint> [buffersize]\n",
  "\n",
  "Freeze a GFS cluster:\n",
  "  gfs_tool freeze <mountpoint>\n",
  "\n",
  "Unfreeze a GFS cluster:\n",
  "  gfs_tool unfreeze <mountpoint>\n",
  "\n",
  "Free unused disk inodes:\n",
  "  gfs_tool reclaim <mountpoint>\n",
  "\n",
  "Do a GFS specific \"df\"\n",
  "  gfs_tool df <mountpoint>\n",
  "\n",
  "Tune a GFS superblock\n",
  "  gfs_tool sb <device> proto [newval]\n",
  "  gfs_tool sb <device> table [newval]\n",
  "  gfs_tool sb <device> ondisk [newval]\n",
  "  gfs_tool sb <device> multihost [newval]\n",
  "  gfs_tool sb <device> all\n",
  "\n",
  "Tune a running filesystem\n",
  "  gfs_tool gettune <mountpoint>\n",
  "  gfs_tool settune <mountpoint> <parameter> <value>\n",
  "\n",
  "Set a flag on a inode\n",
  "  gfs_tool setflag flag <filenames>\n",
  "\n",
  "Clear a flag on a inode\n",
  "  gfs_tool clearflag flag <filenames>\n",
  "\n",
  "Print the counters for a filesystem\n",
  "  gfs_tool counters <mountpoint>\n",
  "\n",
  "Force files from a machine's cache\n",
  "  gfs_tool flush <filenames>\n",
  "\n",
  "Print tool version information\n",
  "  gfs_tool version\n",
  ""
};





/**
 * print_usage - print out usage information
 *
 */

void print_usage()
{
  int x;

  for (x = 0; usage[x][0]; x++)
    printf(usage[x]);
}


/**
 * print_flags - print the flags in a dinode's di_flags field
 * @di: the dinode structure
 *
 */

static void print_flags(struct gfs_dinode *di)
{
  if (di->di_flags)
  {
    printf("Flags:\n");
    if (di->di_flags & GFS_DIF_JDATA)
      printf("  jdata\n");
    if (di->di_flags & GFS_DIF_EXHASH)
      printf("  exhash\n");
    if (di->di_flags & GFS_DIF_UNUSED)
      printf("  unused\n");
    if (di->di_flags & GFS_DIF_EA_INDIRECT)
      printf("  ea_indirect\n");
    if (di->di_flags & GFS_DIF_DIRECTIO)
      printf("  directio\n");
#if 0
    if (di->di_flags & GFS_DIF_IMMUTABLE)
      printf("  immutable\n");
    if (di->di_flags & GFS_DIF_APPENDONLY)
      printf("  appendonly\n");
    if (di->di_flags & GFS_DIF_NOATIME)
      printf("  noatime\n");
    if (di->di_flags & GFS_DIF_SYNC)
      printf("  sync\n");
#endif
    if (di->di_flags & GFS_DIF_INHERIT_DIRECTIO)
      printf("  inherit_directio\n");
    if (di->di_flags & GFS_DIF_INHERIT_JDATA)
      printf("  inherit_jdata\n");
  }
}


/**
 * check_for_gfs - Check to see if a descriptor is a file on a GFS filesystem
 * @fd: the file descriptor
 * @path: the path used to open the descriptor
 *
 */

void check_for_gfs(int fd, char *path)
{
  unsigned int magic = 0;
  int error;

  error = ioctl(fd, GFS_WHERE_ARE_YOU, &magic);
  if (error || magic != GFS_MAGIC)
    die("%s is not a GFS file/filesystem\n", path);
}


/**
 * print_stat - print out the struct gfs_dinode for a file
 * @argc:
 * @argv:
 *
 */

static void print_stat(int argc, char **argv)
{
  struct gfs_dinode di;
  int fd;

  if (argc != 3)
    die("Usage: gfs_tool stat <filename>\n");

  fd = open(argv[2], O_RDONLY);
  if (fd < 0)
    die("can't open %s:  %s\n", argv[2], strerror(errno));

  check_for_gfs(fd, argv[2]);

  if (ioctl(fd, GFS_FILE_STAT, &di) < 0)
    die("error doing ioctl:  %s\n", strerror(errno));

  close(fd);

  gfs_dinode_print(&di);
  printf("\n");
  print_flags(&di);
}


/**
 * print_sb - the superblock
 * @argc:
 * @argv:
 *
 */

static void print_sb(int argc, char **argv)
{
  struct gfs_sb sb;
  int fd;

  if (argc != 3)
    die("Usage: gfs_tool getsb <mountpoint>\n");

  fd = open(argv[2], O_RDONLY);
  if (fd < 0)
    die("can't open %s:  %s\n", argv[2], strerror(errno));

  check_for_gfs(fd, argv[2]);

  if (ioctl(fd, GFS_GET_SUPER, &sb) < 0)
    die("error doing ioctl:  %s\n", strerror(errno));

  close(fd);

  gfs_sb_print(&sb);
}


/**
 * print_jindex - print out the journal index
 * @argc:
 * @argv:
 *
 */

static void print_jindex(int argc, char **argv)
{
  struct gfs_jio jt;
  struct gfs_dinode di;
  struct gfs_jindex ji;
  char buf[sizeof(struct gfs_jindex)];
  uint64 offset;
  unsigned int x = 0;
  int fd;


  memset(&jt, 0, sizeof(struct gfs_jio));
  jt.jio_file = GFS_HIDDEN_JINDEX;

  if (argc != 3)
    die("Usage: gfs_tool jindex <mountpoint>\n");

  fd = open(argv[2], O_RDONLY);
  if (fd < 0)
    die("can't open %s:  %s\n", argv[2], strerror(errno));

  check_for_gfs(fd, argv[2]);


  jt.jio_data = (char *)&di;
  jt.jio_size = sizeof(struct gfs_dinode);

  if (ioctl(fd, GFS_JSTAT, &jt) < 0)
    die("error doing ioctl:  %s\n", strerror(errno));

  gfs_dinode_print(&di);


  for (offset = 0; ; offset += sizeof(struct gfs_jindex), x++)
  {
    jt.jio_data = buf;
    jt.jio_offset = offset;
    jt.jio_size = sizeof(struct gfs_jindex);

    if (ioctl(fd, GFS_JREAD, &jt) < 0)
      die("error doing ioctl:  %s\n", strerror(errno));

    if (!jt.jio_count)
      break;
    if (jt.jio_count != sizeof(struct gfs_jindex))
      die("corrupt journal index\n");

    gfs_jindex_in(&ji, buf);

    printf("\nJournal %u:\n\n", x);

    gfs_jindex_print(&ji);
  }


  close(fd);
}


/**
 * print_rindex - print out the journal index
 * @argc:
 * @argv:
 *
 */

static void print_rindex(int argc, char **argv)
{
  struct gfs_jio jt;
  struct gfs_dinode di;
  struct gfs_rindex ri;
  char buf[sizeof(struct gfs_rindex)];
  uint64 offset;
  unsigned int x = 0;
  int fd;


  memset(&jt, 0, sizeof(struct gfs_jio));
  jt.jio_file = GFS_HIDDEN_RINDEX;

  if (argc != 3)
    die("Usage: gfs_tool rindex <mountpoint>\n");

  fd = open(argv[2], O_RDONLY);
  if (fd < 0)
    die("can't open %s:  %s\n", argv[2], strerror(errno));

  check_for_gfs(fd, argv[2]);


  jt.jio_data = (char *)&di;
  jt.jio_size = sizeof(struct gfs_dinode);

  if (ioctl(fd, GFS_JSTAT, &jt) < 0)
    die("error doing ioctl:  %s\n", strerror(errno));

  gfs_dinode_print(&di);


  for (offset = 0; ; offset += sizeof(struct gfs_rindex), x++)
  {
    jt.jio_data = buf;
    jt.jio_offset = offset;
    jt.jio_size = sizeof(struct gfs_rindex);

    if (ioctl(fd, GFS_JREAD, &jt) < 0)
      die("error doing ioctl:  %s\n", strerror(errno));

    if (!jt.jio_count)
      break;
    if (jt.jio_count != sizeof(struct gfs_rindex))
      die("corrupt resource index\n");

    gfs_rindex_in(&ri, buf);

    printf("\nResource Group %u:\n\n", x);

    gfs_rindex_print(&ri);
  }


  close(fd);
}


/**
 * print_quota - print out the journal index
 * @argc:
 * @argv:
 *
 */

static void print_quota(int argc, char **argv)
{
  struct gfs_jio jt;
  struct gfs_dinode di;
  struct gfs_quota q;
  char buf[sizeof(struct gfs_quota)];
  uint64 offset;
  unsigned int x = 0;
  int fd;


  memset(&jt, 0, sizeof(struct gfs_jio));
  jt.jio_file = GFS_HIDDEN_QUOTA;

  if (argc != 3)
    die("Usage: gfs_tool quota <mountpoint>\n");

  fd = open(argv[2], O_RDONLY);
  if (fd < 0)
    die("can't open %s:  %s\n", argv[2], strerror(errno));

  check_for_gfs(fd, argv[2]);


  jt.jio_data = (char *)&di;
  jt.jio_size = sizeof(struct gfs_dinode);

  if (ioctl(fd, GFS_JSTAT, &jt) < 0)
    die("error doing ioctl:  %s\n", strerror(errno));

  gfs_dinode_print(&di);


  for (offset = 0; ; offset += sizeof(struct gfs_quota), x++)
  {
    jt.jio_data = buf;
    jt.jio_offset = offset;
    jt.jio_size = sizeof(struct gfs_quota);

    if (ioctl(fd, GFS_JREAD, &jt) < 0)
      die("error doing ioctl:  %s\n", strerror(errno));

    if (!jt.jio_count)
      break;
    if (jt.jio_count != sizeof(struct gfs_quota))
      die("corrupt resource index\n");

    gfs_quota_in(&q, buf);

    printf("\nQuota Entry %u:\n\n", x);

    gfs_quota_print(&q);
  }


  close(fd);
}


/**
 * shrink - shrink the inode cache for a filesystem
 * @argc:
 * @argv:
 *
 */

static void shrink(int argc, char **argv)
{
  int fd;

  if (argc != 3)
    die("Usage: gfs_tool shrink <mountpoint>\n");

  fd = open(argv[2], O_RDONLY);
  if (fd < 0)
    die("can't open %s:  %s\n", argv[2], strerror(errno));

  check_for_gfs(fd, argv[2]);

  if (ioctl(fd, GFS_SHRINK, NULL) < 0)
    die("error doing ioctl:  %s\n", strerror(errno));

  close(fd);
}


/**
 * dump_lockstate - dump lockstate to the debug buffer
 * @argc:
 * @argv:
 *
 */

static void dump_lockstate(int argc, char **argv)
{
  struct gfs_user_buffer ub;
  int fd;
  int retry = TRUE;


  memset(&ub, 0, sizeof(struct gfs_user_buffer));
  ub.ub_size = 4194304;


  if (argc == 4)
  {
    ub.ub_size = atoi(argv[3]);
    retry = FALSE;
  }
  else if (argc != 3)
    die("Usage: gfs_tool lockdump <mountpoint> [buffersize]\n");


  fd = open(argv[2], O_RDONLY);
  if (fd < 0)
    die("can't open %s:  %s\n", argv[2], strerror(errno));

  check_for_gfs(fd, argv[2]);


  for (;;)
  {
    ub.ub_data = malloc(ub.ub_size);
    if (!ub.ub_data)
      die("out of memory\n");

    if (ioctl(fd, GFS_LOCK_DUMP, &ub) < 0)
    {
      if (errno == ENOMEM)
      {
	if (retry)
	{
	  free(ub.ub_data);
	  ub.ub_size += 4194304;
	  fprintf(stderr, "Retrying...\n");
	  continue;
	}
	else
	  die("%u bytes isn't enough memory\n", ub.ub_size);
      }
      die("error doing ioctl:  %s\n", strerror(errno));
    }

    break;
  }


  write(STDOUT_FILENO, ub.ub_data, ub.ub_count);

  free(ub.ub_data);


  close(fd);
}


/**
 * freeze_cluster - freeze a GFS filesystem
 * @argc:
 * @argv:
 *
 * This routine uses an ioctl command to quiesce the GFS cluster.  It
 * forces all machines in the cluster to flush all data and metadata
 * and clean up their journals.
 */

static void freeze_cluster(int argc, char **argv)
{
  int fd;

  if (argc != 3)
    die("Usage: gfs_tool freeze <mountpoint>\n");

  fd = open(argv[2], O_RDONLY);
  if (fd < 0)
    die("can't open %s:  %s\n", argv[2], strerror(errno));

  check_for_gfs(fd, argv[2]);

  if (ioctl(fd, GFS_FREEZE, NULL) < 0)
    die("error doing ioctl:  %s\n", strerror(errno));
  sync();

  close(fd);
}


/**
 * unfreeze_cluster - unfreeze a GFS filesystem
 * @argc:
 * @argv:
 *
 */

static void unfreeze_cluster(int argc, char **argv)
{
  int fd;

  if (argc != 3)
    die("Usage: gfs_tool unfreeze <mountpoint>\n");

  fd = open(argv[2], O_RDONLY);
  if (fd < 0)
    die("can't open %s:  %s\n", argv[2], strerror(errno));

  check_for_gfs(fd, argv[2]);

  if (ioctl(fd, GFS_UNFREEZE, NULL) < 0)
    die("error doing ioctl:  %s\n", strerror(errno));

  close(fd);
}


/**
 * reclaim_metadata - reclaim unused metadata blocks
 * @argc:
 * @argv:
 *
 * This routine uses an ioctl command to quiesce the cluster and then
 * hunt down and free all disk inodes that have been freed.  This will
 * gain back meta data blocks to be used for data (or metadata) again.
 */

static void reclaim_metadata(int argc, char **argv)
{
  struct gfs_reclaim_stats stats;
  char input[256];
  int fd;

  if (argc != 3)
    die("Usage: gfs_tool reclaim <mountpoint>\n");

  fd = open(argv[2], O_RDONLY);
  if (fd < 0)
    die("can't open %s:  %s\n", argv[2], strerror(errno));

  check_for_gfs(fd, argv[2]);

  printf("Don't do this if this file system is being exported by NFS (on any machine).\n");
  printf("\nAre you sure you want to proceed? [y/n] ");
  fgets(input, 255, stdin);

  if (input[0] != 'y')
    die("aborted\n");

  printf("\n");

  if (ioctl(fd, GFS_RECLAIM_METADATA, &stats) < 0)
    die("error doing ioctl:  %s\n", strerror(errno));

  close(fd);

  printf("Reclaimed:\n");
  printf("%"PRIu64" inodes\n", stats.rc_inodes);
  printf("%"PRIu64" metadata blocks\n", stats.rc_metadata);
}


/**
 * do_df_one - print out information about one filesystem
 * @path: the path to the filesystem
 *
 */

static void do_df_one(char *path)
{
  int fd;
  struct gfs_usage usage;
  struct gfs_args args;
  struct gfs_sb sb;
  struct gfs_dinode di;
  struct gfs_jio jt;
  uint64 used_data;
  uint64 journals, rgrps;
  struct lm_lockstruct ls;
  unsigned int percentage;


  fd = open(path, O_RDONLY);
  if (fd < 0)
    die("can't open %s: %s\n", path, strerror(errno));

  check_for_gfs(fd, path);


  if (ioctl(fd, GFS_STATGFS, &usage))
    die("error doing GFS_STATGFS ioctl: %s\n", strerror(errno));


  if (ioctl(fd, GFS_GET_ARGS, &args))
    die("error doing GFS_GET_ARGS: %s\n", strerror(errno));


  if (ioctl(fd, GFS_GET_SUPER, &sb))
    die("error doing GFS_GET_SUPER ioctl: %s\n", strerror(errno));


  memset(&jt, 0, sizeof(struct gfs_jio));
  jt.jio_file = GFS_HIDDEN_JINDEX;
  jt.jio_size = sizeof(struct gfs_dinode);
  jt.jio_data = (char *)&di;

  if (ioctl(fd, GFS_JSTAT, &jt))
    die("error doing GFS_JSTAT ioctl: %s\n", strerror(errno));

  journals = di.di_size;
  if (journals % sizeof(struct gfs_jindex))
    die("bad jindex size\n");
  journals /= sizeof(struct gfs_jindex);


  memset(&jt, 0, sizeof(struct gfs_jio));
  jt.jio_file = GFS_HIDDEN_RINDEX;
  jt.jio_size = sizeof(struct gfs_dinode);
  jt.jio_data = (char *)&di;

  if (ioctl(fd, GFS_JSTAT, &jt))
    die("error doing GFS_JSTAT ioctl: %s\n", strerror(errno));

  rgrps = di.di_size;
  if (rgrps % sizeof(struct gfs_rindex))
    die("bad rindex size\n");
  rgrps /= sizeof(struct gfs_rindex);


  used_data = usage.gu_total_blocks -
    usage.gu_free -
      (usage.gu_used_dinode + usage.gu_free_dinode) -
	(usage.gu_used_meta + usage.gu_free_meta);


  if (ioctl(fd, GFS_GET_LOCKSTRUCT, &ls))
    die("error doing GFS_GET_LOCKSTRUCT ioctl: %s\n", strerror(errno));


  printf("%s:\n", path);
  printf("  SB lock proto = \"%s\"\n", sb.sb_lockproto);
  printf("  SB lock table = \"%s\"\n", sb.sb_locktable);
  printf("  SB ondisk format = %u\n", sb.sb_fs_format);
  printf("  SB multihost format = %u\n", sb.sb_multihost_format);
  printf("  Block size = %u\n", usage.gu_block_size);
  printf("  Journals = %"PRIu64"\n", journals);
  printf("  Resource Groups = %"PRIu64"\n", rgrps);
  printf("  Mounted lock proto = \"%s\"\n", (*args.ar_lockproto) ? args.ar_lockproto : sb.sb_lockproto);
  printf("  Mounted lock table = \"%s\"\n", (*args.ar_locktable) ? args.ar_locktable : sb.sb_locktable);
  printf("  Mounted host data = \"%s\"\n", args.ar_hostdata);
  printf("  Journal number = %u\n", ls.ls_jid);
  printf("  Lock module flags = ");
  if (ls.ls_flags & LM_LSFLAG_LOCAL)
    printf("local ");
  if (ls.ls_flags & LM_LSFLAG_ASYNC)
    printf("async ");
  printf("\n");
  printf("  Local flocks = %s\n", (args.ar_localflocks) ? "TRUE" : "FALSE");
  printf("  Local caching = %s\n", (args.ar_localcaching) ? "TRUE" : "FALSE");
  printf("\n");
  printf("  %-15s%-15s%-15s%-15s%-15s\n", "Type", "Total", "Used", "Free", "use%");
  printf("  ------------------------------------------------------------------------\n");

  percentage = (usage.gu_used_dinode + usage.gu_free_dinode) ?
    (100.0 * usage.gu_used_dinode / (usage.gu_used_dinode + usage.gu_free_dinode) + 0.5) : 0;
  printf("  %-15s%-15"PRIu64"%-15"PRIu64"%-15"PRIu64"%u%%\n",
	 "inodes",
	 usage.gu_used_dinode + usage.gu_free_dinode,
	 usage.gu_used_dinode,
	 usage.gu_free_dinode,
	 percentage);

  percentage = (usage.gu_used_meta + usage.gu_free_meta) ?
    (100.0 * usage.gu_used_meta / (usage.gu_used_meta + usage.gu_free_meta) + 0.5) : 0;
  printf("  %-15s%-15"PRIu64"%-15"PRIu64"%-15"PRIu64"%u%%\n",
	 "metadata",
	 usage.gu_used_meta + usage.gu_free_meta,
	 usage.gu_used_meta,
	 usage.gu_free_meta,
	 percentage);

  percentage = (used_data + usage.gu_free) ?
    (100.0 * used_data / (used_data + usage.gu_free) + 0.5) : 0;
  printf("  %-15s%-15"PRIu64"%-15"PRIu64"%-15"PRIu64"%u%%\n",
	 "data",
	 used_data + usage.gu_free,
	 used_data,
	 usage.gu_free,
	 percentage);


  close(fd);
}


/**
 * do_df - print out information about filesystems
 * @argc:
 * @argv:
 *
 */

static void do_df(int argc, char **argv)
{
  int first = TRUE;

  if (argc == 3)
  {
    char buf[PATH_MAX];

    if (!realpath(argv[2], buf))
      die("can't determine real path: %s\n", strerror(errno));

    do_df_one(buf);
  }
  else if (argc == 2)
  {
#ifdef __linux__
    char buf[256], device[256], path[256], type[256];
    FILE *file;

    file = fopen("/proc/mounts", "r");
    if (!file)
      die("can't open /proc/mounts: %s\n", strerror(errno));

    while (fgets(buf, 256, file))
    {
      if (sscanf(buf, "%s %s %s", device, path, type) != 3)
	continue;
      if (strcmp(type, "gfs") != 0)
	continue;

      if (first)
	first = FALSE;
      else
	printf("\n");

      do_df_one(path);
    }

    fclose(file);
#endif  /*  __linux__  */

#ifdef __FreeBSD__
    struct statfs *st;
    int x, num;

    num = getmntinfo(&st, MNT_NOWAIT);
    if (num < 0)
      die("can't getmntinfo: %s\n", strerror(errno));

    for (x = 0; x < num; x++)
      if (strcmp(st[x].f_fstypename, "gfs") == 0)
      {
	if (first)
	  first = FALSE;
	else
	  printf("\n");

	do_df_one(st[x].f_mntonname);
      }
#endif  /*  __FreeBSD__  */
  }
  else
  {
    die("Usage: gfs_tool df <mountpoint>\n");
  }
}


/**
 * set_flag - set or clear flags in some dinodes
 * @argc:
 * @argv:
 *
 */

static void set_flag(int argc, char *argv[])
{
  struct gfs_dinode di;
  unsigned int cmd;
  uint32 flag;
  unsigned int arg;
  int fd;
  int error;

  if (argc <  3)
  {
    di.di_flags = 0xFFFFFFFF;
    print_flags(&di);
    return;
  }

  cmd = (strcmp(argv[1], "setflag") == 0) ? GFS_SET_FLAG : GFS_CLEAR_FLAG;

  if (strcmp(argv[2], "jdata") == 0)
    flag = GFS_DIF_JDATA;
  else if (strcmp(argv[2], "exhash") == 0)
    flag = GFS_DIF_EXHASH;
  else if (strcmp(argv[2], "unused") == 0)
    flag = GFS_DIF_UNUSED;
  else if (strcmp(argv[2], "ea_indirect") == 0)
    flag = GFS_DIF_EA_INDIRECT;
  else if (strcmp(argv[2], "directio") == 0)
    flag = GFS_DIF_DIRECTIO;
#if 0
  else if (strcmp(argv[2], "immutable") == 0)
    flag = GFS_DIF_IMMUTABLE;
  else if (strcmp(argv[2], "appendonly") == 0)
    flag = GFS_DIF_APPENDONLY;
  else if (strcmp(argv[2], "noatime") == 0)
    flag = GFS_DIF_NOATIME;
  else if (strcmp(argv[2], "sync") == 0)
    flag = GFS_DIF_SYNC;
#endif
  else if (strcmp(argv[2], "inherit_directio") == 0)
    flag = GFS_DIF_INHERIT_DIRECTIO;
  else if (strcmp(argv[2], "inherit_jdata") == 0)
    flag = GFS_DIF_INHERIT_JDATA;
  else
    die("unknown flags %s (run \"gfs_tool setflag\" to see valid flags)\n", argv[2]);

  for (arg = 3; arg < argc; arg++)
  {
    fd = open(argv[arg], O_RDONLY);
    if (fd < 0)
      die("can't open file %s: %s\n", argv[arg], strerror(errno));

    check_for_gfs(fd, argv[arg]);

    error = ioctl(fd, cmd, &flag);
    if (error)
      die("can't change flag on %s: %s\n", argv[arg], strerror(errno));

    close(fd);
  }
}


/**
 * file_flush - 
 * @argc:
 * @argv:
 *
 */

static void file_flush(int argc, char *argv[])
{
  int arg;
  int fd;

  if (argc < 3)
    die("Usage: gfs_tool flush <filenames>\n");

  for (arg = 2; arg < argc; arg++)
  {
    fd = open(argv[arg], O_RDONLY);
    if (fd < 0)
      die("can't open %s:  %s\n", argv[arg], strerror(errno));

    check_for_gfs(fd, argv[arg]);

    if (ioctl(fd, GFS_FILE_FLUSH, NULL) < 0)
      die("error doing ioctl:  %s\n", strerror(errno));

    close(fd);
  }
}


/**
 * print_version -
 * @argc:
 * @argv:
 *
 */

static void print_version(int argc, char **argv)
{
  printf("gfs_tool %s (built %s %s)\n", GFS_RELEASE_NAME,
         __DATE__, __TIME__);
  printf("%s\n", REDHAT_COPYRIGHT);
}


/**
 * main - Do everything
 * @argc:
 * @argv:
 *
 */

int main(int argc,char *argv[])
{
  prog_name = argv[0];


  if (argc < 2)
  {
    print_usage();
    exit(EXIT_SUCCESS);
  }


  if (FALSE)
  {
    /*  Do Nothing  */
  }
#ifdef DEBUG_STACK
  else if (strcmp(argv[1], "stack") == 0)
  {
    print_stack(argc, argv);
  }
#endif
  else if (strcmp(argv[1], "shrink") == 0)
  {
    shrink(argc, argv);
  }
  else if (strcmp(argv[1], "stat") == 0)
  {
    print_stat(argc, argv);
  }
  else if (strcmp(argv[1], "getsb") == 0)
  {
    print_sb(argc, argv);
  }
  else if (strcmp(argv[1], "jindex") == 0)
  {
    print_jindex(argc, argv);
  }
  else if (strcmp(argv[1], "rindex") == 0)
  {
    print_rindex(argc, argv);
  }
  else if (strcmp(argv[1], "quota") == 0)
  {
    print_quota(argc, argv);
  }
  else if (strcmp(argv[1], "layout") == 0)
  {
    print_layout(argc, argv);
  }
  else if (strcmp(argv[1], "lockdump") == 0)
  {
    dump_lockstate(argc, argv);
  }
  else if (strcmp(argv[1], "freeze") == 0)
  {
    freeze_cluster(argc, argv);
  }
  else if (strcmp(argv[1], "unfreeze") == 0)
  {
    unfreeze_cluster(argc, argv);
  }
  else if (strcmp(argv[1], "reclaim") == 0)
  {
    reclaim_metadata(argc, argv);
  }
  else if (strcmp(argv[1], "df") == 0)
  {
    do_df(argc, argv);
  }
  else if (strcmp(argv[1], "sb") == 0)
  {
    do_sb(argc, argv);
  }
  else if (strcmp(argv[1], "gettune") == 0)
  {
    get_tune(argc, argv);
  }
  else if (strcmp(argv[1], "settune") == 0)
  {
    set_tune(argc, argv);
  }
  else if (strcmp(argv[1], "setflag") == 0 || strcmp(argv[1], "clearflag") == 0)
  {
    set_flag(argc, argv);
  }
  else if (strcmp(argv[1], "counters") == 0)
  {
    get_counters(argc, argv);
  }
  else if (strcmp(argv[1], "flush") == 0)
  {
    file_flush(argc, argv);
  }
  else if (strcmp(argv[1], "version") == 0 || strcmp(argv[1], "-V") == 0)
  {
    print_version(argc, argv);
  }
  else if (strcmp(argv[1], "-h") == 0 ||
	   strcmp(argv[1], "--help") == 0)
  {
    print_usage();
  }
  else
    die("%s: invalid option -- %s\nPlease use '-h' for usage.\n", 
        argv[0], argv[1]);



  exit(EXIT_SUCCESS);
}

