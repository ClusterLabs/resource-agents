/*
 * Copyright (C) 2005 Red Hat, Inc.  All rights reserved.
 *
 * This copyrighted material is made available to anyone wishing to use,
 * modify, copy, or redistribute it subject to the terms and conditions
 * of the GNU General Public License v.2.
 */

#include "util.h"

char *prog_name;
char *fsname;
int verbose;

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

static void read_options(int argc, char **argv, struct mount_options *mo)
{
	int cont = 1;
	int optchar;

	/* FIXME: check for "quiet" option and don't print in that case */

	while (cont) {
		optchar = getopt(argc, argv, "hVv");

		switch (optchar) {
		case EOF:
			cont = 0;
			break;

		case 'v':
			++verbose;
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
		strncpy(mo->dir, argv[optind], PATH_MAX);
}

static void check_options(struct mount_options *mo)
{
	if (!strlen(mo->dir))
		die("no mount point specified\n");
}

static void umount_lockproto(char *proto, struct mount_options *mo,
			     struct gfs2_sb *sb)
{
	if (!strcmp(proto, "lock_dlm"))
		lock_dlm_leave(mo, sb);
}

int main(int argc, char **argv)
{
	struct mount_options mo;
	struct gfs2_sb sb;
	char *proto;
	int rv;

	memset(&mo, 0, sizeof(mo));
	memset(&sb, 0, sizeof(sb));

	prog_name = argv[0];

	if (!strstr(prog_name, "gfs"))
		die("invalid umount helper name \"%s\"\n", prog_name);

	fsname = (strstr(prog_name, "gfs2")) ? "gfs2" : "gfs";

	if (argc < 2) {
		print_usage();
		exit(EXIT_SUCCESS);
	}

	read_options(argc, argv, &mo);
	check_options(&mo);
	read_proc_mounts(&mo);
	get_sb(mo.dev, &sb);
	parse_opts(&mo);

	rv = umount(mo.dir);
	if (rv)
		die("error %d unmounting %s\n", errno, mo.dir);

	proto = select_lockproto(&mo, &sb);
	umount_lockproto(proto, &mo, &sb);

	return 0;
}

