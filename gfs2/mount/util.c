/*
 * Copyright (C) 2005 Red Hat, Inc.  All rights reserved.
 *
 * This copyrighted material is made available to anyone wishing to use,
 * modify, copy, or redistribute it subject to the terms and conditions
 * of the GNU General Public License v.2.
 */

#include "util.h"

extern char *prog_name;
extern char *fsname;
extern int verbose;

#define LOCK_DLM_SOCK_PATH "lock_dlmd_sock"	/* FIXME: use a header */
#define MAXLINE 256			/* size of messages with lock_dlmd */

/* opt_map stuff from util-linux */

struct opt_map {
	char *opt;      /* option name */
	int skip;       /* skip in mtab option string (gfs: not used) */
	int inv;	/* true if flag value should be inverted */
	int mask;       /* flag mask value */
};

static struct opt_map opt_map[] = {
  { "defaults", 0, 0, 0	 },      /* default options */
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
  { NULL,       0, 0, 0	 }
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

		log_debug("  %s flag %x for \"%s\", flags = %x",
		       	  om->inv ? "clear" : "set", om->mask, om->opt, *flags);

		return 1;
	}

	return 0;
}

/* opts is the string of all mount options, this function finds
   the options that have MS_XXX flags and sets the appropriate flag
   bit.  The options without an MS_ flag are copied into the extra
   string.  The values of some specific options are saved for later
   internal use. */

void parse_opts(struct mount_options *mo)
{
	char data[PATH_MAX+1];
	char *options, *o, *v;
	int extra_len = 0;

	log_debug("parse_opts: opts = \"%s\"", mo->opts);

	memset(data, 0, sizeof(data));
	strncpy(data, mo->opts, PATH_MAX);

	for (options = data; (o = strsep(&options, ",")); ) {
		if (!*o)
			continue;

		if (set_flag(o, &mo->flags))
			continue;

		if (extra_len + 1 + strlen(o) > PATH_MAX)
			die("extra options string is too long\n");

		if (mo->extra[0]) {
			strcat(mo->extra, ",");
			extra_len += 1;
		}

		log_debug("  add extra %s", o);

		strcat(mo->extra, o);
		extra_len += strlen(o);

		v = strchr(o, '=');
		if (v)
			*v++ = 0;

		/* we grab these now so we don't have to parse them out
		   again later when doing proto-specific stuff */

		if (!strcmp(o, "lockproto")) {
			if (!v)
				die("option lockproto needs value\n");
			strncpy(mo->lockproto, v, 255);
		}

		if (!strcmp(o, "locktable")) {
			if (!v)
				die("option locktable needs value\n");
			strncpy(mo->locktable, v, 255);
		}
	}

	log_debug("parse_opts: flags = %x", mo->flags);
	log_debug("parse_opts: extra = \"%s\"", mo->extra);
	log_debug("parse_opts: lockproto = \"%s\"", mo->lockproto);
	log_debug("parse_opts: locktable = \"%s\"", mo->locktable);
}

void read_proc_mounts(struct mount_options *mo)
{
	FILE *file;
	char line[PATH_MAX];
	char path[PATH_MAX];
	char type[PATH_MAX];
	char opts[PATH_MAX];
	char device[PATH_MAX];
	int found = 0;

	file = fopen("/proc/mounts", "r");
	if (!file)
		die("can't open /proc/mounts: %s\n", strerror(errno));

	while (fgets(line, PATH_MAX, file)) {
		if (sscanf(line, "%s %s %s %s", device, path, type, opts) != 4)
			continue;
		if (strcmp(path, mo->dir))
			continue;
		if (strcmp(type, fsname))
			die("%s is not a %s filesystem\n", mo->dir, fsname);

		strncpy(mo->dev, device, PATH_MAX);
		strncpy(mo->opts, opts, PATH_MAX);
		strncpy(mo->proc_entry, line, PATH_MAX);
		found = 1;
		break;
	}

	fclose(file);

	if (!found)
		die("can't find /proc/mounts entry for directory %s\n", mo->dir);
	log_debug("read_proc_mounts: device = \"%s\"\n", mo->dev);
	log_debug("read_proc_mounts: opts = \"%s\"\n", mo->opts);
}

int get_sb(char *device, struct gfs2_sb *sb_out)
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

	if (!strcmp(fsname, "gfs2")) {
		if (sb.sb_header.mh_magic != GFS2_MAGIC ||
	    	    sb.sb_header.mh_type != GFS2_METATYPE_SB)
			die("there isn't a GFS2 filesystem on %s\n", device);
	} else if (!strcmp(fsname, "gfs")) {
		if (sb.sb_header.mh_magic != GFS_MAGIC ||
		    sb.sb_header.mh_type != GFS_METATYPE_SB)
			die("there isn't a GFS filesystem on %s\n", device);
	}

	memcpy(sb_out, &sb, sizeof(struct gfs2_sb));

	close(fd);
	return 0;
}

char *select_lockproto(struct mount_options *mo, struct gfs2_sb *sb)
{
	/* find the effective lockproto, proto specified in mount options
	   overrides the sb lockproto */

	if (mo->lockproto[0])
		return mo->lockproto;
	else
		return sb->sb_lockproto;
}

static int lock_dlmd_connect(void)
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

int lock_dlm_join(struct mount_options *mo, struct gfs2_sb *sb)
{
	int i, fd, rv;
	char buf[MAXLINE];
	char *dir, *proto, *table, *extra;

	i = 0;
	do {
		sleep(1);
		fd = lock_dlmd_connect();
		if (!fd)
			warn("waiting for lock_dlmd to start\n");
	} while (!fd && ++i < 10);

	/* FIXME: should we start the daemon here? */
	if (!fd)
		die("lock_dlmd daemon not running\n");

	dir = mo->dir;
	proto = "lock_dlm";

	if (mo->locktable[0])
		table = mo->locktable;
	else
		table = sb->sb_locktable;

	if (mo->extra[0])
		extra = mo->extra;
	else
		extra = "-";

	memset(buf, 0, sizeof(buf));
	rv = snprintf(buf, MAXLINE, "join %s %s %s %s %s",
		      dir, fsname, proto, table, extra);
	if (rv >= MAXLINE)
		die("join message too large: %d \"%s\"\n", rv, buf);

	log_debug("lock_dlm_join: write \"%s\"", buf);

	rv = write(fd, buf, sizeof(buf));
	if (rv < 0)
		die("can't communicate with lock_dlm daemon %d\n", rv);

	memset(buf, 0, sizeof(buf));
	rv = read(fd, buf, sizeof(buf));

	log_debug("lock_dlm_join: read1 %d: %s", rv, buf);

	memset(buf, 0, sizeof(buf));
	rv = read(fd, buf, sizeof(buf));

	log_debug("lock_dlm_join: read2 %d: %s", rv, buf);

	/* lock_dlmd returns "hostdata=jid=X:id=Y:first=Z" to add to the
	   extra mount options */

	if (strlen(mo->extra) == 0)
		snprintf(mo->extra_plus, PATH_MAX, "%s", buf);
	else
		snprintf(mo->extra_plus, PATH_MAX, "%s,%s", mo->extra, buf);

	log_debug("lock_dlm_join: extra_plus: \"%s\"", mo->extra_plus);

	return 0;
}

int lock_dlm_leave(struct mount_options *mo, struct gfs2_sb *sb)
{
	int i, fd, rv;
	char buf[MAXLINE];

	i = 0;
	do {
		sleep(1);
		fd = lock_dlmd_connect();
		if (!fd)
			warn("waiting for lock_dlmd to start\n");
	} while (!fd && ++i < 10);

	/* FIXME: not sure what failing here means */

	if (!fd)
		die("lock_dlmd daemon not running\n");

	memset(buf, 0, sizeof(buf));
	rv = snprintf(buf, MAXLINE, "leave %s %s", mo->dir, fsname);
	if (rv >= MAXLINE)
		die("leave message too large: %d \"%s\"\n", rv, buf);

	log_debug("lock_dlm_leave: write \"%s\"", buf);

	rv = write(fd, buf, sizeof(buf));
	if (rv < 0)
		die("can't communicate with lock_dlmd daemon %d", rv);

	memset(buf, 0, sizeof(buf));
	rv = read(fd, buf, sizeof(buf));

	log_debug("lock_dlm_leave: read1 %d: %s\n", rv, buf);

	return 0;
}

