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
  @file Quorum disk /proc/partition scanning functions
 */
#include <stdio.h>
#include <stdlib.h>
#include <disk.h>
#include <errno.h>
#include <sys/types.h>
#include <platform.h>
#include <stdlib.h>
#include <string.h>


int
check_device(char *device, char *label, quorum_header_t *qh)
{
	int fd = -1, ret = -1;
	quorum_header_t qh_local;

	if (!qh)
		qh = &qh_local;

	fd = qdisk_validate(device);
	if (fd < 0) {
		perror("qdisk_verify");
		return -1;
	}

	fd = qdisk_open(device);
	if (fd < 0) {
		perror("qdisk_open");
		return -1;
	}

	if (qdisk_read(fd, OFFSET_HEADER, qh, sizeof(*qh)) == sizeof(*qh)) {
		swab_quorum_header_t(qh);
                if (qh->qh_magic == HEADER_MAGIC_NUMBER) {
			if (!label || !strcmp(qh->qh_cluster, label)) {
				ret = 0;
			}
                }
        }

	qdisk_close(&fd);

	return ret;
}


int
find_partitions(const char *partfile, const char *label,
	        char *devname, size_t devlen, int print)
{
	char line[4096];
	FILE *fp;
	int minor, major;
	unsigned long long blkcnt;
	char device[128];
	char realdev[256];
	quorum_header_t qh;

	fp = fopen(partfile, "r");
	if (!fp)
		return -1;

	while (fgets(line, sizeof(line), fp) != NULL) {
		if (strlen(line) > 128 + (22) /* 5 + 5 + 11 + 1 */) {
			/*printf("Line too long!\n");*/
			continue;
		}

		/* This line is taken from 2.6.15.4's proc line */
		sscanf(line, "%4d %4d %10llu %s", &major, &minor,
		       &blkcnt, device);

		if (strlen(device)) {
			snprintf(realdev, sizeof(realdev),
				 "/dev/%s", device);
			if (check_device(realdev, (char *)label, &qh) != 0)
				continue;

			if (print) {
				time_t timestamp = qh.qh_timestamp;
				printf("%s:\n", realdev);
				printf("\tMagic:   %08x\n", qh.qh_magic);
				printf("\tLabel:   %s\n", qh.qh_cluster);
				printf("\tCreated: %s",
				       ctime((time_t *)&timestamp));
				printf("\tHost:    %s\n\n", qh.qh_updatehost);
			}

			if (devname && devlen) {
				/* Got it */
				strncpy(devname, realdev, devlen);
				fclose(fp);
				return 0;
			}
		}
	}

	fclose(fp);

	if (print)
		/* No errors if we're just printing stuff */
		return 0;

	errno = ENOENT;
	return -1;
}
