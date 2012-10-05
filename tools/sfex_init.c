/*-------------------------------------------------------------------------
 *
 * Shared Disk File EXclusiveness Control Program(SF-EX)
 *
 * sfex_init.c --- Initialize SF-EX meta-data.
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
 * sfex_init [-b <blocksize>] [-n <numlocks>] <device>
 *
 * -b <blocksize> --- The size of the block is specified by the number of 
 * bytes. In general, to prevent a partial writing to the disk, the size 
 * of block is set to 512 bytes etc. 
 * Note a set value because this value is used also for the alignment 
 * adjustment in the input-output buffer in the program when direct I/O 
 * is used(When you specify --enable-directio option for configure script). 
 * (In Linux kernel 2.6, "direct I/O " does not work if this value is not 
 * a multiple of 512.) Default is 512 bytes.
 *
 * -n <numlocks> --- The number of storing lock data is specified by integer 
 * of one or more. When you want to control two or more resources by one 
 * meta-data, you set the value of two or more to numlocks. A necessary disk 
 * area for meta data are (blocksize*(1+numlocks))bytes. Default is 1.
 *
 * <device> --- This is file path which stored meta-data. It is usually 
 * expressed in "/dev/...", because it is partition on the shared disk.
 *
 * exit code --- 0 - Normal end. 3 - Error occurs while processing it. 
 * The content of the error is displayed into stderr. 4 - The mistake is 
 * found in the command line parameter.
 *
 *-------------------------------------------------------------------------*/

#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <getopt.h>

#include "sfex.h"
#include "sfex_lib.h"

const char *progname;
char *nodename;

/*
 * usage --- display command line syntax
 *
 * display command line syntax. By the purpose, it can specify destination 
 * stream. stdout or stderr is set usually.
 *
 * dist --- destination stream of the command line syntax, such as stderr.
 *
 * return value --- void
 */
static void usage(FILE *dist) {
  fprintf(dist, "usage: %s [-n <numlocks>] <device>\n", progname);
}

/*
 * main --- main function
 *
 * entry point of sfex_init command.
 *
 * exit code --- 0 - Normal end. 3 - Error occurs while processing it. 
 * The content of the error is displayed into stderr. 4 - The mistake is 
 * found in the command line parameter.
 */
int
main(int argc, char *argv[]) {
  sfex_controldata cdata;
  sfex_lockdata ldata;

  /* command line parameter */
  int numlocks = 1;		/* default 1 locks  */
  const char *device;

  /*
   *  startup process
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
    int c = getopt(argc, argv, "hn:");
    if (c == -1)
      break;
    switch (c) {
    case 'h':			/* help */
      usage(stdout);
      exit(0);
    case 'n':			/* -n <numlocks> */
      {
	unsigned long l = strtoul(optarg, NULL, 10);
	if (l < SFEX_MIN_NUMLOCKS || l > SFEX_MAX_NUMLOCKS) {
	  fprintf(stderr,
		  "%s: ERROR: numlocks %s is out of range or invalid. it must be integer value between %lu and %lu.\n",
		  progname, optarg,
		  (unsigned long)SFEX_MIN_NUMLOCKS,
		  (unsigned long)SFEX_MAX_NUMLOCKS);
	  exit(4);
	}
	numlocks = l;
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

  prepare_lock(device);

  /* main processes start */

  /* get a node name */
  nodename = get_nodename();

  /* create and control data and lock data */
  init_controldata(&cdata, sector_size, numlocks);
  init_lockdata(&ldata);

  /* write out control data and lock data */
  write_controldata(&cdata);
  {
    int index;
    for (index = 1; index <= numlocks; index++)
      if (write_lockdata(&cdata, &ldata, index) == -1) {
        fprintf(stderr, "%s: ERROR: cannot write lock data (index=%d).\n",
                progname, index);
        exit(3);
      }
  }

  exit(0);
}
