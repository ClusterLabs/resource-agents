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

#include <linux/gfs2_ondisk.h>

#define LOCK_DLM_SOCK_PATH "lock_dlmd_sock"	/* FIXME: use a header */
#define MAXLINE 256			/* size of messages with lock_dlmd */

static char *prog_name;
static char *fsname = "gfs2";

struct mount_options {
	char dev[PATH_MAX+1];
	char dir[PATH_MAX+1];
	char opts[PATH_MAX+1];
	char extra[PATH_MAX+1];
	char extra_plus[PATH_MAX+1];
	char type[5];
	int flags;
};

#define die(fmt, args...) \
do { \
	fprintf(stderr, "%s: ", prog_name); \
	fprintf(stderr, fmt, ##args); \
	exit(EXIT_FAILURE); \
} while (0)

#define do_lseek(fd, off) \
do { \
	if (lseek((fd), (off), SEEK_SET) != (off)) \
		die("bad seek: %s on line %d of file %s\n", \
		    strerror(errno),__LINE__, __FILE__); \
} while (0)

#define do_read(fd, buff, len) \
do { \
	if (read((fd), (buff), (len)) != (len)) \
		die("bad read: %s on line %d of file %s\n", \
		    strerror(errno), __LINE__, __FILE__); \
} while (0)

static int get_sb(char *device, struct gfs2_sb *sb_out)
{
	int fd;
	char buf[GFS2_BASIC_BLOCK];
	struct gfs2_sb sb;

	fd = open(device, O_RDONLY);
	if (fd < 0)
		die("can't open %s: %s\n", device, strerror(errno));

	do_lseek(fd, GFS2_SB_ADDR * GFS2_BASIC_BLOCK);
	do_read(fd, buf, GFS2_BASIC_BLOCK);

	gfs2_sb_in(&sb, buf);

	if (sb.sb_header.mh_magic != GFS2_MAGIC ||
	    sb.sb_header.mh_type != GFS2_METATYPE_SB)
		die("there isn't a GFS2 filesystem on %s\n", device);

	memcpy(sb_out, &sb, sizeof(struct gfs2_sb));

	close(fd);

	return 0;
}

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

static int do_join(struct mount_options *mo, struct gfs2_sb *sb)
{
	int i, fd, rv;
	char buf[MAXLINE];
	char *dir, *type, *proto, *table, *extra;

	i = 0;
	do {
		sleep(1);
		fd = gfs_daemon_connect();
		if (!fd)
			fprintf(stderr, "waiting for gfs daemon to start\n");
	} while (!fd && ++i < 10);

	if (!fd)
		die("gfs daemon not running\n");

	dir = mo->dir;
	type = "gfs2";
	proto = sb->sb_lockproto;
	table = sb->sb_locktable;
	extra = mo->extra;

	if (strlen(extra) == 0)
		extra = "-";

	memset(buf, 0, sizeof(buf));
	rv = snprintf(buf, MAXLINE, "join %s %s %s %s %s",
		      dir, type, proto, table, extra);
	if (rv >= MAXLINE)
		die("join message too large: %d \"%s\"\n", rv, buf);

	printf("do_join: write \"%s\"\n", buf);

	rv = write(fd, buf, sizeof(buf));
	if (rv < 0)
		die("can't communicate with gfs daemon %d", rv);

	memset(buf, 0, sizeof(buf));
	rv = read(fd, buf, sizeof(buf));

	printf("do_join: read1 %d: %s\n", rv, buf);

	memset(buf, 0, sizeof(buf));
	rv = read(fd, buf, sizeof(buf));

	printf("do_join: read2 %d: %s\n", rv, buf);

	/* gfs daemon returns "hostdata=jid=X:id=Y:first=Z" to add to the
	   extra mount options */

	if (strlen(mo->extra) == 0)
		snprintf(mo->extra_plus, PATH_MAX, "%s", buf);
	else
		snprintf(mo->extra_plus, PATH_MAX, "%s,%s", mo->extra, buf);

	printf("do_join: extra_plus: \"%s\"\n", mo->extra_plus);

	return 0;
}

static int do_cluster(struct mount_options *mo)
{
	struct gfs2_sb sb;
	int rv;

	get_sb(mo->dev, &sb);

	rv = do_join(mo, &sb);
	if (rv)
		die("mount failed during cluster init %d\n", rv);

	return 0;
}

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

/* opt_map stuff from util-linux */

struct opt_map {
	char *opt;	/* option name */
	int skip;	/* skip in mtab option string (gfs: not used) */
	int inv;	/* true if flag value should be inverted */
	int mask;	/* flag mask value */
};

static struct opt_map opt_map[] = {
  { "defaults", 0, 0, 0         },      /* default options */
  { "ro",       1, 0, MS_RDONLY },      /* read-only */
  { "rw",       1, 1, MS_RDONLY },      /* read-write */
  { "exec",     0, 1, MS_NOEXEC },      /* permit execution of binaries */
  { "noexec",   0, 0, MS_NOEXEC },      /* don't execute binaries */
  { "suid",     0, 1, MS_NOSUID },      /* honor suid executables */
  { "nosuid",   0, 0, MS_NOSUID },      /* don't honor suid executables */
  { "dev",      0, 1, MS_NODEV  },      /* interpret device files  */
  { "nodev",    0, 0, MS_NODEV  },      /* don't interpret devices */
  { "sync",     0, 0, MS_SYNCHRONOUS},  /* synchronous I/O */
  { "async",    0, 1, MS_SYNCHRONOUS},  /* asynchronous I/O */
  { "remount",  0, 0, MS_REMOUNT},      /* Alter flags of mounted FS */
  { "bind",     0, 0, MS_BIND   },      /* Remount part of tree elsewhere */
  { "mand",     0, 0, MS_MANDLOCK },    /* Allow mandatory locks on this FS */
  { "nomand",   0, 1, MS_MANDLOCK },    /* Forbid mandatory locks on this FS */
  { "atime",    0, 1, MS_NOATIME },     /* Update access time */
  { "noatime",  0, 0, MS_NOATIME },     /* Do not update access time */
  { "diratime", 0, 1, MS_NODIRATIME },  /* Update dir access times */
  { "nodiratime", 0, 0, MS_NODIRATIME },/* Do not update dir access times */
  { NULL,       0, 0, 0         }
};

/* if option has a corresponding MS_XXX, set the bit in the flags */

static int set_flag(char *o, int *flags)
{
	struct opt_map *om;

	for (om = opt_map; om->opt; om++) {
		if (strcmp(om->opt, o))
			continue;

		if (om->inv)
			*flags &= ~om->mask;
		else
			*flags |= om->mask;

		return 1;
	}

	return 0;
}

/* opts is the string of all mount options, this function extracts
   the options that have MS_XXX flags and sets the appropriate flag
   bit.  The options without an MS_ flag are copied into the extra
   string. */

void parse_opts(char *data_arg, int *flags, char *extra)
{
	char *data = data_arg;
	char *options, *o;
	int extra_len = 0;

	for (options = data; (o = strsep(&options, ",")); ) {
		if (!*o)
			continue;

		if (set_flag(o, flags))
			continue;

		if (extra_len + 1 + strlen(o) > PATH_MAX)
			die("extra options string is too long\n");

		if (extra[0]) {
			strcat(extra, ",");
			extra_len += 1;
		}

		strcat(extra, o);
		extra_len += strlen(o);
	}
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
		parse_opts(mo->opts, &mo->flags, mo->extra);
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

	/* FIXME: if REMOUNT, don't do cluster stuff, verify only things
	   changing are flags in MS_RMT_MASK */

	do_cluster(&mo);

	block_signals(SIG_BLOCK);

	rv = mount(mo.dev, mo.dir, fsname, mo.flags, mo.extra_plus);
	if (rv) {
#if 0
		if (!(mo.flags & MS_REMOUNT))
			undo_cluster(&mo);
#endif
		block_signals(SIG_UNBLOCK);

		die("error %d mounting %s on %s\n", errno, mo.dev, mo.dir);
	}

	/* FIXME: are we going to mess with mtab, please no */

	block_signals(SIG_UNBLOCK);

	return rv ? 1 : 0;
}

