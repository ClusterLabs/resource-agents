/** @file
 * Calls ioctl to invalidate/flush buffers on a given device.
 *
 * Author: Gregory P. Myrdal <Myrdal@MissionCriticalLinux.Com>
 *
 * invalidatebuffers.c
 */

/*
 * Version string that is filled in by CVS
 */
static const char *version __attribute__ ((unused)) = "$Revision$";

/*
 * System includes
 */
#include <unistd.h>
#include <stdio.h>
#include <syslog.h>
#include <linux/fs.h>
#include <string.h>
#include <sys/stat.h>
#include <errno.h>

#ifdef __NFDBITS
#undef __NFDBITS
#endif

#ifdef __FDMASK
#undef __FDMASK
#endif

#include <sys/types.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <stdlib.h>
/*
 * Cluster includes
 */
#include <logging.h>

/***************************************************************************
 *
 * Functions
 *
 ***************************************************************************/

/**
 * printUsage
 *
 * Print out usage string to stdout.
 */
static void
printUsage(char *progName)
{
	printf("Usage: %s [-h] [-f device]\n", progName);
}

/***************************************************************************
 *
 * Main
 *
 ***************************************************************************/
int
main(int argc, char **argv)
{
	int opt;
	uid_t uid;
	char *devicename = (char *)NULL;
	int fd;

	uid=getuid();
	if (uid)
	  {
	    printf("%s should only be run as user root\n", argv[0]);
	    exit(1);
	  }

	while ((opt = getopt(argc, argv, "f:h")) != -1)
	  {
	    switch (opt)
	      {
              case 'f':			// stop services
	        devicename = strdup(optarg);
	        break;

              case 'h':			// command line help
                printUsage(argv[0]);
	        exit(0);

              default:			// unknown option
                printUsage(argv[0]);     
	        exit(0);
	      }
	  }

	if (devicename == (char *)NULL)
	  {
	    printUsage(argv[0]);
	    exit(1);
	  }

	fd = open(devicename, O_RDONLY, 0);

	if (fd < 0)
	  {
	    printf("Cannot open %s for flushing: %s\n",
	            devicename, strerror(errno));
	    exit(1);
	  }

	if (ioctl(fd, BLKFLSBUF, 0) < 0)
	  {
	    printf("Cannot flush %s: %s\n", devicename, strerror(errno));
	    exit(1);
	  }
	free(devicename);
	close(fd);           

	exit(0);
}
