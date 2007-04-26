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
	sigdelset(&sigs, SIGINT);
	sigprocmask(how, &sigs, (sigset_t *) 0);
}

static void read_options(int argc, char **argv, struct mount_options *mo)
{
	int cont = 1;
	int optchar;
	char *real;

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

	if (optind < argc && argv[optind]) {
		real = realpath(argv[optind], NULL);
		if (!real)
			die("invalid device path \"%s\"\n", argv[optind]);
		strncpy(mo->dev, real, PATH_MAX);
		free(real);
	}

	++optind;

	if (optind < argc && argv[optind]) {
		real = realpath(argv[optind], NULL);
		if (!real)
			die("invalid mount point path \"%s\"\n", argv[optind]);
		strncpy(mo->dir, real, PATH_MAX);
		free(real);
	}

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

	if (!strcmp(proto, "lock_dlm")) {
		if (mo->flags & MS_REMOUNT) {
			rv = lock_dlm_remount(mo, sb);
			strncpy(mo->extra_plus, mo->extra, PATH_MAX);
		}
		else
			rv = lock_dlm_join(mo, sb);
	} else
		strncpy(mo->extra_plus, mo->extra, PATH_MAX);

	return rv;
}

static void mount_result_lockproto(char *proto, struct mount_options *mo,
			     	    struct gen_sb *sb, int result)
{
	if (!strcmp(proto, "lock_dlm"))
		lock_dlm_mount_result(mo, sb, result);
}

static void umount_lockproto(char *proto, struct mount_options *mo,
			     struct gen_sb *sb, int mnterr)
{
	if (!strcmp(proto, "lock_dlm"))
		lock_dlm_leave(mo, sb, mnterr);
}

static void check_sys_fs(char *fsname)
{
	DIR *d;
	struct dirent *de;

	d = opendir("/sys/fs/");
	if (!d)
		die("no /sys/fs/ directory found: %d\n", errno);

	while ((de = readdir(d))) {
		if (strnlen(fsname, 5) != strnlen(de->d_name, 5))
			continue;
		if (!strncmp(fsname, de->d_name, strnlen(fsname, 5)))
			return;
	}
	die("fs type \"%s\" not found in /sys/fs/, is the module loaded?\n",
	    fsname);
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

	check_sys_fs(fsname);

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
		log_debug("mount(2) failed error %d errno %d", rv, errno);
		mount_result_lockproto(proto, &mo, &sb, rv);

		if (!(mo.flags & MS_REMOUNT))
			umount_lockproto(proto, &mo, &sb, errno);

		block_signals(SIG_UNBLOCK);
		if (errno == EBUSY)
			die("%s already mounted or %s busy\n", mo.dev, mo.dir);
		die("error %d mounting %s on %s\n", errno, mo.dev, mo.dir);
	}
	log_debug("mount(2) ok");
	mount_result_lockproto(proto, &mo, &sb, 0);

	block_signals(SIG_UNBLOCK);

	if (!(mo.flags & MS_REMOUNT))
		add_mtab_entry(&mo);

	return rv ? 1 : 0;
}

