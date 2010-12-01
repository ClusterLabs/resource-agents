/*-------------------------------------------------------------------------
 *
 * Shared Disk File EXclusiveness Control Program(SF-EX)
 *
 * sfex_stat.c --- Display lock status. This is a part of the SF-EX.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  
 * 02110-1301, USA.
 *
 * Copyright (c) 2007 NIPPON TELEGRAPH AND TELEPHONE CORPORATION
 *
 * $Id$
 *
 *-------------------------------------------------------------------------
 *
 * sfex_stat [-i <index>] <device>
 *
 * -i <index> --- The index is number of the resource that display the lock.
 * This number is specified by the integer of one or more. When two or more 
 * resources are exclusively controlled by one meta-data, this option is used. 
 * Default is 1.
 *
 * <device> --- This is file path which stored meta-data. It is usually 
 * expressed in "/dev/...", because it is partition on the shared disk.
 *
 * exit code --- 0 - Normal end. Own node is holding lock. 2 - Normal 
 * end. Own node does not hold a lock. 3 - Error occurs while processing 
 * it. The content of the error is displayed into stderr. 4 - The mistake 
 * is found in the command line parameter.
 *
 *-------------------------------------------------------------------------*/

#include <config.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <errno.h>
#include <string.h>
#include <limits.h>
#if HAVE_UNISTD_H
#  include <unistd.h>
#endif

#include "sfex.h"
#include "sfex_lib.h"

const char *progname;
char *nodename;

void print_controldata(const sfex_controldata *cdata);
void print_lockdata(const sfex_lockdata *ldata, int index);

/*
 * print_controldata --- print sfex control data to the display
 *
 * print sfex_controldata to the display.
 *
 * cdata --- pointer for control data
 */
void
print_controldata(const sfex_controldata *cdata)
{
  printf("control data:\n");
  printf("  magic: 0x%02x, 0x%02x, 0x%02x, 0x%02x\n",
         cdata->magic[0], cdata->magic[1], cdata->magic[2], cdata->magic[3]);
  printf("  version: %d\n", cdata->version);
  printf("  revision: %d\n", cdata->revision);
  printf("  blocksize: %d\n", (int)cdata->blocksize);
  printf("  numlocks: %d\n", cdata->numlocks);
}

/*
 * print_lockdata --- print sfex lock data to the display
 *
 * print sfex_lockdata to the display.
 *
 * ldata --- pointer for lock data
 *
 * index --- index number
 */
void
print_lockdata(const sfex_lockdata *ldata, int index)
{
  printf("lock data #%d:\n", index);
  printf("  status: %s\n", ldata->status == SFEX_STATUS_UNLOCK ? "unlock" : "lock");
  printf("  count: %d\n", ldata->count);
  printf("  nodename: %s\n",ldata->nodename);
}

/*
 * usage --- display command line syntax
 *
 * display command line syntax. By the purpose, it can specify destination 
 * stream. stdout or stderr is set usually.
 *
 * dist --- destination stream of the command line syntax, such as stderr.
 *
 * retrun value --- void
 */
static void usage(FILE *dist) {
  fprintf(dist, "usage: %s [-i <index>] <device>\n", progname);
}

/*
 * main --- main function
 *
 * entry point of sfex_stat command.
 *
 * exit code --- 0 - Normal end. Own node is holding lock. 2 - Normal 
 * end. Own node dose not hold a lock. 3 - Error occurs while processing 
 * it. The content of the error is displayed into stderr. 4 - The mistake 
 * is found in the command line parameter.
 */
int
main(int argc, char *argv[]) {
  sfex_controldata cdata;
  sfex_lockdata ldata;
  int ret = 0;

  /* command line parameter */
  int index = 1;		/* default 1st lock */
  const char *device;

  /*
   * startup process
   */

  /* get a program name */
  progname = get_progname(argv[0]);

  /* enable the cl_log output from the sfex library */
  cl_log_set_entity(progname);
  /* The cl_log is output only to the standard error output */
  cl_log_enable_stderr(TRUE);

  /* read command line option */
  opterr = 0;
  while (1) {
    int c = getopt(argc, argv, "hi:");
    if (c == -1)
      break;
    switch (c) {
    case 'h':			/* help */
      usage(stdout);
      exit(0);
    case 'i':			/* -i <index> */
      {
	unsigned long l = strtoul(optarg, NULL, 10);
	if (l < SFEX_MIN_NUMLOCKS || l > SFEX_MAX_NUMLOCKS) {
	  fprintf(stderr,
		  "%s: ERROR: index %s is out of range or invalid. it must be integer value between %lu and %lu.\n",
		  progname, optarg,
		  (unsigned long)SFEX_MIN_NUMLOCKS,
		  (unsigned long)SFEX_MAX_NUMLOCKS);
	  exit(4);
	}
	index = l;
      }
      break;
    case '?':			/* error */
      usage(stderr);
      exit(4);
    }
  }

  /* check parameter except the option */
  if (optind >= argc) {
    fprintf(stderr, "%s: ERROR: no device specified.\n", progname);
    usage(stderr);
    exit(4);
  } else if (optind + 1 < argc) {
    fprintf(stderr, "%s: ERROR: too many arguments.\n", progname);
    usage(stderr);
    exit(4);
  }
  device = argv[optind];

  /*
   * main processes start 
   */

  /* get a node name */
  nodename = get_nodename();

  prepare_lock(device);

  ret = lock_index_check(&cdata, index);
  if (ret == -1)
    exit(EXIT_FAILURE);

  /* read lock data */
  read_lockdata(&cdata, &ldata, index);

  /* display status */
  print_controldata(&cdata);
  print_lockdata(&ldata, index);

  /* check current lock status */
  if (ldata.status != SFEX_STATUS_LOCK || strcmp(ldata.nodename, nodename)) {
    fprintf(stdout, "status is UNLOCKED.\n");
    exit(2);
  } else {
    fprintf(stdout, "status is LOCKED.\n");
    exit(0);
  }
}
