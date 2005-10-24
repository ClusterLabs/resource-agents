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

/* libmount.a */
void parse_opts(const char *options, int *flags, char **extra_opts);

static char *prog_name;
static char *fsname = "gfs2";

struct mount_options {
	char dev[PATH_MAX+1];
	char dir[PATH_MAX+1];
	char opts[PATH_MAX+1];
	char *extra;
	char type[5];
	int flags;
};

#define die(fmt, args...) \
do { \
        fprintf(stderr, "%s: ", prog_name); \
        fprintf(stderr, fmt, ##args); \
        exit(EXIT_FAILURE); \
} while (0)


static void print_version(void)
{
	printf("mount.gfs2 %s (built %s %s)\n", GFS2_RELEASE_NAME,
	       __DATE__, __TIME__);
}

static void print_usage(void)
{
	printf("Usage:\n");
	printf("This program is called by mount(8), it should not be used directly.\n");
}

static void block_signals(int how)
{
	sigset_t sigs;
	sigfillset(&sigs);
	sigdelset(&sigs, SIGTRAP);
	sigdelset(&sigs, SIGSEGV);
	sigprocmask(how, &sigs, (sigset_t *) 0);
}

static void read_options(int argc, char **argv, struct mount_options *mo)
{
	int cont = 1;
	int optchar;

	/* FIXME: check for "quiet" option and don't print in that case */

	while (cont) {
		optchar = getopt(argc, argv, "hVo:t:");

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

		case 'o':
			if (optarg)
				strncpy(mo->opts, optarg, PATH_MAX);
			break;

		case 't':
			if (optarg)
				strncpy(mo->type, optarg, 4);
			break;

		default:
			break;
		}
	}

	if (optind < argc && argv[optind])
		strncpy(mo->dev, argv[optind], PATH_MAX);

	++optind;

	if (optind < argc && argv[optind])
		strncpy(mo->dir, argv[optind], PATH_MAX);
}

static void process_options(struct mount_options *mo)
{
	if (!strlen(mo->dev))
		die("no device name specified\n");

	if (!strlen(mo->dir))
		die("no mount point specified\n");

	if (strlen(mo->type) && strcmp(mo->type, fsname))
		die("unknown file system type \"%s\"\n", mo->type);

	if (strlen(mo->opts))
		parse_opts(mo->opts, &mo->flags, &mo->extra);
}

int main(int argc, char **argv)
{
	struct mount_options mo;
	int rv;

	prog_name = argv[0];

	if (argc < 2) {
		print_usage();
		exit(EXIT_SUCCESS);
	}

	memset(&mo, 0, sizeof(mo));
	read_options(argc, argv, &mo);
	process_options(&mo);

	/* FIXME: what about remounts? (mo.flags & MS_REMOUNT) */

#if 0
	rv = do_cluster_stuff();
	if (rv)
		die("cluster error\n");
#endif

	block_signals(SIG_BLOCK);

	/* FIXME: do we need to clear certain flags that the kernel
	   doesn't know about (cf MS_NOSYS in mount) ? */

	rv = mount(mo.dev, mo.dir, fsname, mo.flags, mo.extra);
	if (rv) {
#if 0
		if (!(mo.flags & MS_REMOUNT))
			undo_cluster_stuff();
#endif
		block_signals(SIG_UNBLOCK);

		die("error %d mounting %s on %s\n", errno, mo.dev, mo.dir);
	}

	/* FIXME: update mtab, cf mount.c:update_mtab_entry(),
	   would we need fix_opts_string() before that? */

	block_signals(SIG_UNBLOCK);

	return rv ? 1 : 0;
}

