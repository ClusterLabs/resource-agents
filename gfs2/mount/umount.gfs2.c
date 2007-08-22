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
char *expert;
int verbose;
static sigset_t old_sigset;

static void print_version(void)
{
	printf("umount.gfs2 %s (built %s %s)\n", RELEASE_VERSION,
	       __DATE__, __TIME__);
}

static void print_usage(void)
{
	printf("Usage:\n");
	printf("This program is called by umount(8), it should not be used directly.\n");
	printf("If umount(8) fails to call umount.gfs2, you can clean up with\n");
	printf("> umount.gfs2 -v -X lock_dlm <mountpoint>\n");

}

static void block_sigint(void)
{
	sigset_t new;

	sigemptyset(&new);
	sigaddset(&new, SIGINT);
	sigprocmask(SIG_BLOCK, &new, &old_sigset);
}

static void unblock_sigint(void)
{
	sigprocmask(SIG_SETMASK, &old_sigset, NULL);
}

static void read_options(int argc, char **argv, struct mount_options *mo)
{
	int cont = 1;
	int optchar;
	int l;

	/* FIXME: check for "quiet" option and don't print in that case */

	while (cont) {
		optchar = getopt(argc, argv, "fhVvX:");

		switch (optchar) {
		case EOF:
			cont = 0;
			break;

		case 'f':    /* autofs umount from /sbin/halt uses this */
			break;

		case 'v':
			++verbose;
			break;

		case 'X':
			expert = strdup(optarg);
			log_debug("umount expert override: %s", expert);
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

	if (optind < argc && argv[optind]) {
		strncpy(mo->dir, argv[optind], PATH_MAX);
		l = strlen(mo->dir) - 1;
		while (l > 0 && mo->dir[l] == '/') {
			mo->dir[l] = '\0';
			l--;
		};
	}

	log_debug("umount %s", mo->dir);
}

static void check_options(struct mount_options *mo)
{
	if (!strlen(mo->dir))
		die("no mount point specified\n");
}

static int umount_lockproto(char *proto, struct mount_options *mo,
			     struct gen_sb *sb)
{
	int rv = 0;

	if (!strcmp(proto, "lock_dlm"))
		rv = lock_dlm_leave(mo, sb, 0);
	return rv;
}

int main(int argc, char **argv)
{
	struct mount_options mo;
	struct gen_sb sb;
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

	if (expert)
		return umount_lockproto(expert, &mo, &sb);

	check_options(&mo);
	read_proc_mounts(&mo);
	get_sb(mo.dev, &sb);
	parse_opts(&mo);

	block_sigint();

	rv = umount(mo.dir);
	if (rv) {
		if (errno == EBUSY)
			die("%s: device is busy.\n", mo.dir);
		else
			die("error %d unmounting %s\n", errno, mo.dir);
	}
	proto = select_lockproto(&mo, &sb);
	umount_lockproto(proto, &mo, &sb);

	del_mtab_entry(&mo);

	unblock_sigint();

	return 0;
}

