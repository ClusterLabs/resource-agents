/*
 * Copyright (C) 2005 Red Hat, Inc.  All rights reserved.
 *
 * This copyrighted material is made available to anyone wishing to use,
 * modify, copy, or redistribute it subject to the terms and conditions
 * of the GNU General Public License v.2.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <inttypes.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mount.h>
#include <fcntl.h>
#include <unistd.h>
#include <limits.h>
#include <errno.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/un.h>


#define LOCK_DLM_SOCK_PATH "lock_dlmd_sock"  /* FIXME: use a header */
#define MAXLINE 256

static char *prog_name;
static char opt_dir[PATH_MAX+1];

#define die(fmt, args...) \
do { \
	fprintf(stderr, "%s: ", prog_name); \
	fprintf(stderr, fmt, ##args); \
	exit(EXIT_FAILURE); \
} while (0)

static int gfs_daemon_connect(void)
{
	struct sockaddr_un sun;
	socklen_t addrlen;
	int rv, fd;

	fd = socket(PF_UNIX, SOCK_STREAM, 0);
	if (fd < 0)
		goto out;

	memset(&sun, 0, sizeof(sun));
	sun.sun_family = AF_UNIX;
	strcpy(&sun.sun_path[1], LOCK_DLM_SOCK_PATH);
	addrlen = sizeof(sa_family_t) + strlen(sun.sun_path+1) + 1;

	rv = connect(fd, (struct sockaddr *) &sun, addrlen);
	if (rv < 0) {
		close(fd);
		fd = rv;
	}
 out:
	return fd;
}

static int do_leave(void)
{
	int i, fd, rv;
	char buf[MAXLINE];

	i = 0;
	do {
		sleep(1);
		fd = gfs_daemon_connect();
		if (!fd)
			fprintf(stderr, "waiting for gfs daemon to start\n");
	} while (!fd && ++i < 10);

	if (!fd)
		die("gfs daemon not running\n");

	memset(buf, 0, sizeof(buf));
	rv = snprintf(buf, MAXLINE, "leave %s gfs2", opt_dir);
	if (rv >= MAXLINE)
		die("leave message too large: %d \"%s\"\n", rv, buf);

	printf("do_leave: write \"%s\"\n", buf);

	rv = write(fd, buf, sizeof(buf));
	if (rv < 0)
		die("can't communicate with gfs daemon %d", rv);

	memset(buf, 0, sizeof(buf));
	rv = read(fd, buf, sizeof(buf));

	printf("do_leave: read1 %d: %s\n", rv, buf);

	return 0;
}

static void print_version(void)
{
	printf("umount.gfs2 %s (built %s %s)\n", GFS2_RELEASE_NAME,
	       __DATE__, __TIME__);
}

static void print_usage(void)
{
	printf("Usage:\n");
	printf("This program is called by umount(8), it should not be used directly.\n");
}

static void read_options(int argc, char **argv)
{
	int cont = 1;
	int optchar;

	/* FIXME: check for "quiet" option and don't print in that case */

	while (cont) {
		optchar = getopt(argc, argv, "hV");

		switch (optchar) {
		case EOF:
			cont = 0;
			break;

		case 'h':
			print_usage();
			exit(EXIT_SUCCESS);

		case 'V':
			print_version();
			exit(EXIT_SUCCESS);

		default:
			break;
		}
	}

	if (optind < argc && argv[optind])
		strncpy(opt_dir, argv[optind], PATH_MAX);
}

int main(int argc, char **argv)
{
	int rv;

	prog_name = argv[0];

	if (argc < 2) {
		print_usage();
		exit(EXIT_SUCCESS);
	}

	memset(&opt_dir, 0, sizeof(opt_dir));
	read_options(argc, argv);

	if (!strlen(opt_dir))
		die("no mount point specified\n");

	rv = umount(opt_dir);
	if (rv)
		die("error %d unmounting %s\n", errno, opt_dir);

	/* FIXME: update mtab? */

	do_leave();

	return 0;
}

