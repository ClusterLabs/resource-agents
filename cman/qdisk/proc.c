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
#include <sys/stat.h>
#include <platform.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>

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
find_partitions(const char *devdir, const char *label,
	        char *devname, size_t devlen, int print)
{
	struct dirent **namelist;
	struct stat sb;
	char newpath[256];
	int n;
	quorum_header_t qh;

	n = scandir(devdir, &namelist, 0, alphasort);
	if (n <= 0)
		return -1;

	while (n--) {
		/* filter out:
		 * . and ..
		 * .static and .udev that are typical udev dirs that we don't want to scan
		 */
		if (strcmp(namelist[n]->d_name, ".") &&
		    strcmp(namelist[n]->d_name, "..") &&
		    strcmp(namelist[n]->d_name, ".static") &&
		    strcmp(namelist[n]->d_name, ".udev")) {
			snprintf(newpath, sizeof(newpath), "%s/%s", devdir, namelist[n]->d_name);
			if (!lstat(newpath, &sb)) {
				/* dive into directories */
				if (S_ISDIR(sb.st_mode)) {
					if (!find_partitions(newpath, label, devname, devlen, print)) {
						if (devname && (strlen(devname) > 0))
							return 0;
					}
				}
				/* check if it's a block device */
				if (S_ISBLK(sb.st_mode)) {
					if (!check_device(newpath, (char *)label, &qh)) {
						if (print) {
							time_t timestamp = qh.qh_timestamp;
							printf("%s:\n", newpath);
							printf("\tMagic:   %08x\n", qh.qh_magic);
							printf("\tLabel:   %s\n", qh.qh_cluster);
							printf("\tCreated: %s",
								ctime((time_t *)&timestamp));
							printf("\tHost:    %s\n\n", qh.qh_updatehost);
						}

						if (devname && devlen) {
							strncpy(devname, newpath, devlen);
							return 0;
						}
					}
				}
			}
		}
		free(namelist[n]);
	}

	errno = ENOENT;
	return -1;
}
