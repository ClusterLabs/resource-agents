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
		optchar = getopt(argc, argv, "hVo:t:v");

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

		case 'v':
			++verbose;
			break;

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

	log_debug("mount %s %s", mo->dev, mo->dir);
}

static void check_options(struct mount_options *mo)
{
	struct stat buf;

	if (!strlen(mo->dev))
		die("no device name specified\n");

	if (!strlen(mo->dir))
		die("no mount point specified\n");

	if (strlen(mo->type) && strcmp(mo->type, fsname))
		die("unknown file system type \"%s\"\n", mo->type);

	if (stat(mo->dir, &buf) < 0)
		die("mount point %s does not exist\n", mo->dir);

	if (!S_ISDIR(buf.st_mode))
		die("mount point %s is not a directory\n", mo->dir);
}

static int mount_lockproto(char *proto, struct mount_options *mo,
			   struct gen_sb *sb)
{
	int rv = 0;

	/* FIXME: what should we do here? */
	if (mo->flags & MS_REMOUNT)
		return 0;

	if (!strcmp(proto, "lock_dlm"))
		rv = lock_dlm_join(mo, sb);
	else
		strncpy(mo->extra_plus, mo->extra, PATH_MAX);

	return rv;
}

static void umount_lockproto(char *proto, struct mount_options *mo,
			     struct gen_sb *sb)
{
	if (!strcmp(proto, "lock_dlm"))
		lock_dlm_leave(mo, sb);
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
		die("invalid mount helper name \"%s\"\n", prog_name);

	fsname = (strstr(prog_name, "gfs2")) ? "gfs2" : "gfs";

	if (argc < 2) {
		print_usage();
		exit(EXIT_SUCCESS);
	}

	read_options(argc, argv, &mo);
	check_options(&mo);
	get_sb(mo.dev, &sb);
	parse_opts(&mo);

	proto = select_lockproto(&mo, &sb);

	rv = mount_lockproto(proto, &mo, &sb);
	if (rv < 0)
		die("error mounting lockproto %s\n", proto);

	block_signals(SIG_BLOCK);

	rv = mount(mo.dev, mo.dir, fsname, mo.flags, mo.extra_plus);
	if (rv) {
		if (!(mo.flags & MS_REMOUNT))
			umount_lockproto(proto, &mo, &sb);

		block_signals(SIG_UNBLOCK);

		die("error %d mounting %s on %s\n", errno, mo.dev, mo.dir);
	}

	block_signals(SIG_UNBLOCK);

	log_debug("mount syscall returned %d", rv);

	if (!(mo.flags & MS_REMOUNT))
		add_mtab_entry(&mo);

	return rv ? 1 : 0;
}

