/**
  Copyright Red Hat, Inc. 2006

  This program is free software; you can redistribute it and/or modify it
  under the terms of the GNU General Public License as published by the
  Free Software Foundation; either version 2, or (at your option) any
  later version.

  This program is distributed in the hope that it will be useful, but
  WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; see the file COPYING.  If not, write to the
  Free Software Foundation, Inc.,  675 Mass Ave, Cambridge, 
  MA 02139, USA.

  Author: Lon Hohberger <lhh at redhat.com>
 */
/**
  @file Quorum disk utility
 */
#include <stdio.h>
#include <stdlib.h>
#include <disk.h>
#include <errno.h>
#include <sys/types.h>
#include <platform.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>


int
main(int argc, char **argv)
{
	char device[128];
	char *newdev = NULL, *newlabel = NULL;
	int rv;

	printf("mkqdisk v0.5.1\n");

	while ((rv = getopt(argc, argv, "Lf:c:l:h")) != EOF) {
		switch (rv) {
		case 'L':
			/* List */
			close(2);
			return find_partitions("/proc/partitions",
					       NULL, NULL, 0, 1);
			break;
		case 'f':
			close(2);
			return find_partitions("/proc/partitions",
					       optarg, device,
					       sizeof(device), 1);
		case 'c':
			newdev = optarg;
			break;
		case 'l':
			newlabel = optarg;
			break;
		case 'h':
			printf("usage: mkqdisk -L | -f <label> | -c "
			       "<device> -l <label>\n");
			return 0;
		default:
			break;
		}
	}

	if (!newdev && !newlabel) {
		printf("usage: mkqdisk -L | -f <label> | -c "
		       "<device> -l <label>\n");
		return 1;
	}

	if (!newdev || !newlabel) {
		printf("Both a device and a label are required\n");
		return 1;
	}

	printf("Writing new quorum disk label '%s' to %s.\n",
	       newlabel, newdev);
	printf("WARNING: About to destroy all data on %s; proceed [N/y] ? ",
	       newdev);
	if (getc(stdin) != 'y') {
		printf("Good thinking.\n");
		return 0;
	}

	return qdisk_init(newdev, newlabel);
}
