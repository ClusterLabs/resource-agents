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
#include <sys/stat.h>
#include <unistd.h>
#include <ctype.h>

#include "global.h"
#include <linux/gfs_ioctl.h>

#include "gfs_tool.h"



#if GFS_TUNE_VERSION != (((0) << 16) | (139))
#error update GFS/src/tool/gfs_tool/tune.c
#endif





/**
 * atou - convert an ascii number into an unsigned int
 * @c: the ascii number
 *
 * Returns: The unsigned int
 */

static unsigned int atou(const char *c)
{
  unsigned int x = 0;
            
  while ('0' <= *c && *c <= '9')
  {
    x = x * 10 + (*c - '0');
    c++;
  }
            
  return x;
}


/**
 * get_tune - print out the current tuneable parameters for a filesystem
 * @argc:
 * @argv:
 *
 */

void get_tune(int argc, char **argv)
{
  struct gfs_tune gt;
  int fd;
  int error;


  if (argc < 3)
    die("Usage: gfs_tool gettune <mountpoint>\n");


  fd = open(argv[2], O_RDONLY);
  if (fd < 0)
    die("can't open file %s: %s\n", argv[2], strerror(errno));

  check_for_gfs(fd, argv[2]);

  error = ioctl(fd, GFS_GET_TUNE, &gt);
  if (error)
    die("can't get tune parameters: %s\n", strerror(errno));

  close(fd);


  if (gt.gt_tune_version != GFS_TUNE_VERSION)
    die("gfs_tool and gfs versions are mismatched\n");


  printf("ilimit1 = %u\n", gt.gt_ilimit1);
  printf("ilimit1_tries = %u\n", gt.gt_ilimit1_tries);
  printf("ilimit1_min = %u\n", gt.gt_ilimit1_min);
  printf("ilimit2 = %u\n", gt.gt_ilimit2);
  printf("ilimit2_tries = %u\n", gt.gt_ilimit2_tries);
  printf("ilimit2_min = %u\n", gt.gt_ilimit2_min);
  printf("demote_secs = %u\n", gt.gt_demote_secs);
  printf("incore_log_blocks = %u\n", gt.gt_incore_log_blocks);
  printf("jindex_refresh_secs = %u\n", gt.gt_jindex_refresh_secs);
  printf("depend_secs = %u\n", gt.gt_depend_secs);
  printf("scand_secs = %u\n", gt.gt_scand_secs);
  printf("recoverd_secs = %u\n", gt.gt_recoverd_secs);
  printf("logd_secs = %u\n", gt.gt_logd_secs);
  printf("quotad_secs = %u\n", gt.gt_quotad_secs);
  printf("inoded_secs = %u\n", gt.gt_inoded_secs);
  printf("quota_simul_sync = %u\n", gt.gt_quota_simul_sync);
  printf("quota_warn_period = %u\n", gt.gt_quota_warn_period);
  printf("atime_quantum = %u\n", gt.gt_atime_quantum);
  printf("quota_quantum = %u\n", gt.gt_quota_quantum);
  printf("quota_scale = %.4f   (%u, %u)\n",
	 (double)gt.gt_quota_scale_num / gt.gt_quota_scale_den,
	 gt.gt_quota_scale_num, gt.gt_quota_scale_den);
  printf("quota_enforce = %u\n", gt.gt_quota_enforce);
  printf("quota_account = %u\n", gt.gt_quota_account);
  printf("new_files_jdata = %u\n", gt.gt_new_files_jdata);
  printf("new_files_directio = %u\n", gt.gt_new_files_directio);
  printf("max_atomic_write = %u\n", gt.gt_max_atomic_write);
  printf("max_readahead = %u\n", gt.gt_max_readahead);
  printf("lockdump_size = %u\n", gt.gt_lockdump_size);
  printf("stall_secs = %u\n", gt.gt_stall_secs);
  printf("complain_secs = %u\n", gt.gt_complain_secs);
  printf("reclaim_limit = %u\n", gt.gt_reclaim_limit);
  printf("entries_per_readdir = %u\n", gt.gt_entries_per_readdir);
  printf("prefetch_secs = %u\n", gt.gt_prefetch_secs);
  printf("statfs_slots = %u\n", gt.gt_statfs_slots);
  printf("max_mhc = %u\n", gt.gt_max_mhc);
  printf("greedy_default = %u\n", gt.gt_greedy_default);
  printf("greedy_max = %u\n", gt.gt_greedy_max);
  printf("greedy_quantum = %u\n", gt.gt_greedy_quantum);
}


/**
 * minimize - minimize a fraction
 * @numerator: the fraction's numerator
 * @denominator: the fraction's denominator
 *
 */

static void minimize(unsigned int *numerator, unsigned int *denominator)
{
  /*  Fill me in one day when you're bored.  */
}


/**
 * set_tune - set a tuneable parameter
 * @argc:
 * @argv:
 *
 */

void set_tune(int argc, char **argv)
{
  struct gfs_tune gt;
  int fd;
  int error;


  if (argc < 5)
    die("Usage: gfs_tool settune <mountpoint> <parameter> <value>\n");


  fd = open(argv[2], O_RDONLY);
  if (fd < 0)
    die("can't open file %s: %s\n", argv[2], strerror(errno));

  check_for_gfs(fd, argv[2]);

  error = ioctl(fd, GFS_GET_TUNE, &gt);
  if (error)
    die("can't get tune parameters: %s\n", strerror(errno));


  if (gt.gt_tune_version != GFS_TUNE_VERSION)
    die("gfs_tool and gfs versions are mismatched\n");


  if (strcmp(argv[3], "ilimit1") == 0)
    gt.gt_ilimit1 = atou(argv[4]);

  else if (strcmp(argv[3], "ilimit1_tries") == 0)
    gt.gt_ilimit1_tries = atou(argv[4]);

  else if (strcmp(argv[3], "ilimit1_min") == 0)
    gt.gt_ilimit1_min = atou(argv[4]);

  else if (strcmp(argv[3], "ilimit2") == 0)
    gt.gt_ilimit2 = atou(argv[4]);

  else if (strcmp(argv[3], "ilimit2_tries") == 0)
    gt.gt_ilimit2_tries = atou(argv[4]);

  else if (strcmp(argv[3], "ilimit2_min") == 0)
    gt.gt_ilimit2_min = atou(argv[4]);

  else if (strcmp(argv[3], "demote_secs") == 0)
    gt.gt_demote_secs = atou(argv[4]);

  else if (strcmp(argv[3], "incore_log_blocks") == 0)
    gt.gt_incore_log_blocks = atou(argv[4]);

  else if (strcmp(argv[3], "jindex_refresh_secs") == 0)
    gt.gt_jindex_refresh_secs = atou(argv[4]);

  else if (strcmp(argv[3], "depend_secs") == 0)
    gt.gt_depend_secs = atou(argv[4]);

  else if (strcmp(argv[3], "scand_secs") == 0)
    gt.gt_scand_secs = atou(argv[4]);

  else if (strcmp(argv[3], "recoverd_secs") == 0)
    gt.gt_recoverd_secs = atou(argv[4]);

  else if (strcmp(argv[3], "logd_secs") == 0)
    gt.gt_logd_secs = atou(argv[4]);

  else if (strcmp(argv[3], "quotad_secs") == 0)
    gt.gt_quotad_secs = atou(argv[4]);

  else if (strcmp(argv[3], "inoded_secs") == 0)
    gt.gt_inoded_secs = atou(argv[4]);

  else if (strcmp(argv[3], "quota_simul_sync") == 0)
    gt.gt_quota_simul_sync = atou(argv[4]);

  else if (strcmp(argv[3], "quota_warn_period") == 0)
    gt.gt_quota_warn_period = atou(argv[4]);

  else if (strcmp(argv[3], "atime_quantum") == 0)
    gt.gt_atime_quantum = atou(argv[4]);

  else if (strcmp(argv[3], "quota_quantum") == 0)
    gt.gt_quota_quantum = atou(argv[4]);

  else if (strcmp(argv[3], "quota_scale") == 0)
  {
    gt.gt_quota_scale_num = ((atof(argv[4]) < 0) ? 0.0 : atof(argv[4])) * 10000 + 0.5;
    gt.gt_quota_scale_den = 10000;

    minimize(&gt.gt_quota_scale_num, &gt.gt_quota_scale_den);
  }

  else if (strcmp(argv[3], "quota_enforce") == 0)
  {
    gt.gt_quota_enforce = !!atou(argv[4]);
    if (gt.gt_quota_enforce)
      gt.gt_quota_account = 1;
  }

  else if (strcmp(argv[3], "quota_account") == 0)
  {
    gt.gt_quota_account = !!atou(argv[4]);
    if (!gt.gt_quota_account)
    {
      gt.gt_quota_enforce = 0;

      /*  Get all the quota tags out of the log.  */

      sync();
      sleep(1);
      error = ioctl(fd, GFS_QUOTA_SYNC, NULL);
      if (error)
	die("can't sync quotas: %s\n", strerror(errno));
    }
  }

  else if (strcmp(argv[3], "new_files_jdata") == 0)
    gt.gt_new_files_jdata = !!atou(argv[4]);

  else if (strcmp(argv[3], "new_files_directio") == 0)
    gt.gt_new_files_directio = !!atou(argv[4]);

  else if (strcmp(argv[3], "max_atomic_write") == 0)
    gt.gt_max_atomic_write = atou(argv[4]);

  else if (strcmp(argv[3], "max_readahead") == 0)
    gt.gt_max_readahead = atou(argv[4]);

  else if (strcmp(argv[3], "lockdump_size") == 0)
    gt.gt_lockdump_size = atou(argv[4]);

  else if (strcmp(argv[3], "stall_secs") == 0)
    gt.gt_stall_secs = atou(argv[4]);

  else if (strcmp(argv[3], "complain_secs") == 0)
    gt.gt_complain_secs = atou(argv[4]);

  else if (strcmp(argv[3], "reclaim_limit") == 0)
    gt.gt_reclaim_limit = atou(argv[4]);

  else if (strcmp(argv[3], "entries_per_readdir") == 0)
    gt.gt_entries_per_readdir = atou(argv[4]);

  else if (strcmp(argv[3], "prefetch_secs") == 0)
    gt.gt_prefetch_secs = atou(argv[4]);

  else if (strcmp(argv[3], "statfs_slots") == 0)
    gt.gt_statfs_slots = atou(argv[4]);

  else if (strcmp(argv[3], "max_mhc") == 0)
    gt.gt_max_mhc = atou(argv[4]);

  else if (strcmp(argv[3], "greedy_default") == 0)
    gt.gt_greedy_default = atou(argv[4]);

  else if (strcmp(argv[3], "greedy_max") == 0)
    gt.gt_greedy_max = atou(argv[4]);

  else if (strcmp(argv[3], "greedy_quantum") == 0)
    gt.gt_greedy_quantum = atou(argv[4]);

  else
    die("unknown tuning parameter\n");


  error = ioctl(fd, GFS_SET_TUNE, &gt);
  if (error)
    die("can't set tune parameters: %s\n", strerror(errno));

  close(fd);


  get_tune(argc, argv);
}


