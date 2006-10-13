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
static int gfs_controld_fd;

#define LOCK_DLM_SOCK_PATH "gfs_controld_sock"	/* FIXME: use a header */
#define MAXLINE 256			/* size of messages with gfs_controld */

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

		if (!strncmp("hostdata", o, 8)) {
			if (mo->hostdata[0])
				warn("duplicate hostdata strings");
			else
				strcat(mo->hostdata, o);
			continue;
		}

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
	log_debug("parse_opts: hostdata = \"%s\"", mo->hostdata);
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
	log_debug("read_proc_mounts: device = \"%s\"", mo->dev);
	log_debug("read_proc_mounts: opts = \"%s\"", mo->opts);
}

void gfs2_inum_in(struct gfs2_inum *no, char *buf)
{
	struct gfs2_inum *str = (struct gfs2_inum *)buf;

	no->no_formal_ino = be64_to_cpu(str->no_formal_ino);
	no->no_addr = be64_to_cpu(str->no_addr);
}

void gfs2_meta_header_in(struct gfs2_meta_header *mh, char *buf)
{
	struct gfs2_meta_header *str = (struct gfs2_meta_header *)buf;

	mh->mh_magic = be32_to_cpu(str->mh_magic);
	mh->mh_type = be32_to_cpu(str->mh_type);
	mh->mh_format = be32_to_cpu(str->mh_format);
}

void gfs2_sb_in(struct gfs2_sb *sb, char *buf)
{
	struct gfs2_sb *str = (struct gfs2_sb *)buf;

	gfs2_meta_header_in(&sb->sb_header, buf);

	sb->sb_fs_format = be32_to_cpu(str->sb_fs_format);
	sb->sb_multihost_format = be32_to_cpu(str->sb_multihost_format);
	sb->sb_bsize = be32_to_cpu(str->sb_bsize);
	sb->sb_bsize_shift = be32_to_cpu(str->sb_bsize_shift);

	gfs2_inum_in(&sb->sb_master_dir, (char *)&str->sb_master_dir);
	gfs2_inum_in(&sb->sb_root_dir, (char *)&str->sb_root_dir);

	memcpy(sb->sb_lockproto, str->sb_lockproto, GFS2_LOCKNAME_LEN);
	memcpy(sb->sb_locktable, str->sb_locktable, GFS2_LOCKNAME_LEN);
}

int get_sb(char *device, struct gen_sb *sb_out)
{
	int fd;

	fd = open(device, O_RDONLY);
	if (fd < 0)
		die("can't open %s: %s\n", device, strerror(errno));

	if (!strcmp(fsname, "gfs2")) {
		char buf[GFS2_BASIC_BLOCK];
		struct gfs2_sb sb;

		do_lseek(fd, GFS2_SB_ADDR * GFS2_BASIC_BLOCK);
		do_read(fd, buf, GFS2_BASIC_BLOCK);
		gfs2_sb_in(&sb, buf);

		if (sb.sb_header.mh_magic != GFS2_MAGIC ||
	    	    sb.sb_header.mh_type != GFS2_METATYPE_SB) {
			die("there isn't a GFS2 filesystem on %s, "
			    "magic=%x type=%x\n", device,
			    sb.sb_header.mh_magic, sb.sb_header.mh_type);
		}

		if (sb.sb_fs_format != GFS2_FORMAT_FS ||
		    sb.sb_multihost_format != GFS2_FORMAT_MULTI) {
			die("there appears to be a GFS, not GFS2, filesystem "
			    "on %s\n", device);
		}

		strncpy(sb_out->lockproto, sb.sb_lockproto, 256);
		strncpy(sb_out->locktable, sb.sb_locktable, 256);

	} else if (!strcmp(fsname, "gfs")) {
		char buf[GFS_BASIC_BLOCK];
		struct gfs_sb sb;

		do_lseek(fd, GFS_SB_ADDR * GFS_BASIC_BLOCK);
		do_read(fd, buf, GFS2_BASIC_BLOCK);
		gfs_sb_in(&sb, buf);

		if (sb.sb_header.mh_magic != GFS_MAGIC ||
		    sb.sb_header.mh_type != GFS_METATYPE_SB) {
			gfs_sb_print(&sb);
			die("there isn't a GFS filesystem on %s\n", device);
		}

		if (sb.sb_fs_format != GFS_FORMAT_FS ||
		    sb.sb_multihost_format != GFS_FORMAT_MULTI) {
			die("there appears to be a GFS2, not GFS, filesystem "
			    "on %s\n", device);
		}

		strncpy(sb_out->lockproto, sb.sb_lockproto, 256);
		strncpy(sb_out->locktable, sb.sb_locktable, 256);
	}

	close(fd);
	return 0;
}

char *select_lockproto(struct mount_options *mo, struct gen_sb *sb)
{
	/* find the effective lockproto, proto specified in mount options
	   overrides the sb lockproto */

	if (mo->lockproto[0])
		return mo->lockproto;
	else
		return sb->lockproto;
}

static int gfs_controld_connect(void)
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

#if 0
/* We create a pipe and pass the receiving end to gfs_controld.  If the
   mount fails, we write an error message to this pipe.  gfs_controld monitors
   this fd outside its main poll loop because it may need to detect a mount
   failure while watching for the kernel mount (while waiting for the kernel
   mount, gfs_controld is _not_ in its main poll loop which is why the normal
   leave message w/ mnterr we send isn't sufficient.) */

void setup_mount_error_fd(int socket)
{
	struct msghdr msg;
	struct cmsghdr *cmsg;
	struct iovec vec;
	char tmp[CMSG_SPACE(sizeof(int))];
	char ch = '\0';
	ssize_t n;
	int rv, fds[2];

	rv = pipe(fds);
	if (rv < 0) {
		log_debug("setup_mount_error_fd pipe error %d %d", rv, errno);
		return;
	}

	memset(&msg, 0, sizeof(msg));

	msg.msg_control = (caddr_t)tmp;
	msg.msg_controllen = CMSG_LEN(sizeof(int));
	cmsg = CMSG_FIRSTHDR(&msg);
	cmsg->cmsg_len = CMSG_LEN(sizeof(int));
	cmsg->cmsg_level = SOL_SOCKET;
	cmsg->cmsg_type = SCM_RIGHTS;
	*(int *)CMSG_DATA(cmsg) = fds[0];

	vec.iov_base = &ch;
	vec.iov_len = 1;
	msg.msg_iov = &vec;
	msg.msg_iovlen = 1;

	n = sendmsg(socket, &msg, 0);
	if (n < 0) {
		log_debug("setup_mount_error_fd sendmsg error %d %d", n, errno);
		close(fds[0]);
		close(fds[1]);
		return;
	}

	mount_error_fd = fds[1];

	log_debug("setup_mount_error_fd %d %d", fds[0], fds[1]);
}
#endif

int lock_dlm_join(struct mount_options *mo, struct gen_sb *sb)
{
	int i, fd, rv;
	char buf[MAXLINE];
	char *dir, *proto, *table, *options;

	i = 0;
	do {
		fd = gfs_controld_connect();
		if (fd < 0) {
			warn("waiting for gfs_controld to start");
			sleep(1);
		}
	} while (fd < 0 && ++i < 10);

	/* FIXME: should we start the daemon here? */
	if (fd < 0) {
		warn("gfs_controld not running");
		rv = -1;
		goto out;
	}

	dir = mo->dir;
	proto = "lock_dlm";
	options = mo->opts;

	if (mo->locktable[0])
		table = mo->locktable;
	else
		table = sb->locktable;

	/*
	 * send request to gfs_controld for it to join mountgroup:
	 * "join <mountpoint> gfs2 lock_dlm <locktable> <options>"
	 */

	memset(buf, 0, sizeof(buf));
	rv = snprintf(buf, MAXLINE, "join %s %s %s %s %s",
		      dir, fsname, proto, table, options);
	if (rv >= MAXLINE) {
		warn("lock_dlm_join: message too long: %d \"%s\"", rv, buf);
		rv = -1;
		goto out;
	}

	log_debug("message to gfs_controld: asking to join mountgroup:");
	log_debug("write \"%s\"", buf);

	rv = write(fd, buf, sizeof(buf));
	if (rv < 0) {
		warn("lock_dlm_join: gfs_controld write error: %d", rv);
		goto out;
	}

#if 0
	setup_mount_error_fd(fd);
#endif

	/*
	 * read response from gfs_controld to our join request:
	 * it sends back an int as a string, 0 or -EXXX
	 */

	memset(buf, 0, sizeof(buf));
	rv = read(fd, buf, sizeof(buf));
	if (rv < 0) {
		warn("lock_dlm_join: gfs_controld read 1 error: %d", rv);
		goto out;
	}
	rv = atoi(buf);
	if (rv < 0) {
		warn("lock_dlm_join: gfs_controld join error: %d", rv);
		goto out;
	}

	log_debug("message from gfs_controld: response to join request:");
	log_debug("lock_dlm_join: read \"%s\"", buf);

	/*
	 * read mount-option string from gfs_controld that we are to
	 * use for the mount syscall; or possibly error message
	 */

	memset(buf, 0, sizeof(buf));
	rv = read(fd, buf, sizeof(buf));
	if (rv < 0) {
		warn("gfs_controld options read error: %d", rv);
		goto out;
	}

	log_debug("message from gfs_controld: mount options:");
	log_debug("lock_dlm_join: read \"%s\"", buf);

	/*
	 * gfs_controld returns "hostdata=jid=X:id=Y:first=Z"
	 * this is first combined with any hostdata the user gave on
	 * the command line and then the full hostdata is combined
	 * with the "extra" mount otions into the "extra_plus" string.
	 * If we're not allowed to mount, "error: foo" is returned.
	 */

	if (!strncmp(buf, "error", 5)) {
		warn("%s", buf);
		rv = -1;
		goto out;
	}

	if (strlen(mo->hostdata) + strlen(buf) + 1 > PATH_MAX) {
		warn("hostdata too long");
		rv = -1;
		goto out;
	}

	if (!mo->hostdata[0])
		snprintf(mo->hostdata, PATH_MAX, "%s", buf);
	else {
		char *p = strstr(buf, "=") + 1;
		strcat(mo->hostdata, ":");
		strcat(mo->hostdata, p);
	}

	log_debug("lock_dlm_join: hostdata: \"%s\"", mo->hostdata);

	if (strlen(mo->extra) == 0)
		snprintf(mo->extra_plus, PATH_MAX, "%s", mo->hostdata);
	else
		snprintf(mo->extra_plus, PATH_MAX, "%s,%s",
			 mo->extra, mo->hostdata);

	log_debug("lock_dlm_join: extra_plus: \"%s\"", mo->extra_plus);
	rv = 0;
 out:
#if 0
	close(fd);
#endif
	gfs_controld_fd = fd;
	return rv;
}

void lock_dlm_mount_result(struct mount_options *mo, struct gen_sb *sb,
			   int result)
{
	int rv;
	char buf[MAXLINE];

	memset(buf, 0, sizeof(buf));
	rv = snprintf(buf, MAXLINE, "mount_result %s %s %d", mo->dir, fsname,
		      result);
	if (rv >= MAXLINE) {
		warn("lock_dlm_mount_result: message too long: %d \"%s\"\n",
		     rv, buf);
		goto out;
	}

	log_debug("lock_dlm_mount_result: write \"%s\"", buf);

	rv = write(gfs_controld_fd, buf, sizeof(buf));
	if (rv < 0) {
		warn("lock_dlm_mount_result: gfs_controld write error: %d", rv);
	}
 out:
	close(gfs_controld_fd);
}

int lock_dlm_leave(struct mount_options *mo, struct gen_sb *sb, int mnterr)
{
	int i, fd, rv;
	char buf[MAXLINE];

	i = 0;
	do {
		fd = gfs_controld_connect();
		if (fd < 0) {
			warn("waiting for gfs_controld to start");
			sleep(1);
		}
	} while (!fd && ++i < 10);

	if (fd < 0) {
		warn("gfs_controld not running");
		rv = -1;
		goto out;
	}

	/*
	 * send request to gfs_controld for it to leave mountgroup:
	 * "leave <mountpoint> <fstype> <mnterr>"
	 *
	 * mnterr is 0 if this leave is associated with an unmount.
	 * mnterr is !0 if this leave is due to a failed kernel mount
	 * in which case gfs_controld shouldn't wait for the kernel mount
	 * to complete before doing the leave.
	 */

	memset(buf, 0, sizeof(buf));
	rv = snprintf(buf, MAXLINE, "leave %s %s %d", mo->dir, fsname, mnterr);
	if (rv >= MAXLINE) {
		warn("lock_dlm_leave: message too long: %d \"%s\"\n", rv, buf);
		rv = -1;
		goto out;
	}

	log_debug("message to gfs_controld: asking to leave mountgroup:");
	log_debug("lock_dlm_leave: write \"%s\"", buf);

#if 0
	if (mnterr && mount_error_fd) {
		rv = write(mount_error_fd, buf, sizeof(buf));
		log_debug("lock_dlm_leave: write to mount_error_fd %d", rv);
	}
#endif

	rv = write(fd, buf, sizeof(buf));
	if (rv < 0) {
		warn("lock_dlm_leave: gfs_controld write error: %d", rv);
		goto out;
	}

	/*
	 * read response from gfs_controld to our leave request:
	 * int as a string, 0 or -EXXX
	 */

	memset(buf, 0, sizeof(buf));
	rv = read(fd, buf, sizeof(buf));
	if (rv < 0) {
		warn("lock_dlm_leave: gfs_controld read error: %d", rv);
		goto out;
	}
	rv = atoi(buf);
	if (rv < 0) {
		warn("lock_dlm_leave: gfs_controld leave error: %d", rv);
		goto out;
	}

	log_debug("message from gfs_controld: response to leave request:");
	log_debug("lock_dlm_leave: read \"%s\"", buf);
	rv = 0;
 out:
	close(fd);
	return rv;
}

int lock_dlm_remount(struct mount_options *mo, struct gen_sb *sb)
{
	int i, fd, rv;
	char buf[MAXLINE];
	char *mode;

	i = 0;
	do {
		sleep(1);
		fd = gfs_controld_connect();
		if (!fd)
			warn("waiting for gfs_controld to start");
	} while (!fd && ++i < 10);

	if (!fd) {
		warn("gfs_controld not running");
		rv = -1;
		goto out;
	}

	/* FIXME: how to check for spectator remounts, we want
	   to disallow remount to/from spectator */

	if (mo->flags & MS_RDONLY)
		mode = "ro";
	else
		mode = "rw";

	/*
	 * send request to gfs_controld for it to remount:
	 * "remount <mountpoint> gfs2 <mode>"
	 */

	memset(buf, 0, sizeof(buf));
	rv = snprintf(buf, MAXLINE, "remount %s %s %s", mo->dir, fsname, mode);
	if (rv >= MAXLINE) {
		warn("remount message too large: %d \"%s\"\n", rv, buf);
		rv = -1;
		goto out;
	}

	log_debug("message to gfs_controld: asking to remount:");
	log_debug("lock_dlm_remount: write \"%s\"", buf);

	rv = write(fd, buf, sizeof(buf));
	if (rv < 0) {
		warn("lock_dlm_remount: gfs_controld write error: %d", rv);
		goto out;
	}

	/*
	 * read response from gfs_controld
	 * int as a string
	 * 1: go ahead
	 * -EXXX: error
	 * 0: wait for second result
	 */

	memset(buf, 0, sizeof(buf));
	rv = read(fd, buf, sizeof(buf));
	if (rv < 0) {
		warn("lock_dlm_remount: gfs_controld read1 error: %d", rv);
		goto out;
	}
	rv = atoi(buf);
	if (rv < 0) {
		warn("lock_dlm_remount: gfs_controld remount error: %d", rv);
		goto out;
	}
	if (rv == 1) {
		rv = 0;
		goto out;
	}

	log_debug("message from gfs_controld: response to remount request:");
	log_debug("lock_dlm_remount: read \"%s\"", buf);

	/*
	 * read second result from gfs_controld
	 */

	memset(buf, 0, sizeof(buf));
	rv = read(fd, buf, sizeof(buf));
	if (rv < 0) {
		warn("lock_dlm_remount: gfs_controld read2 error: %d", rv);
		goto out;
	}

	log_debug("message from gfs_controld: remount result:");
	log_debug("lock_dlm_remount: read \"%s\"", buf);

	if (!strncmp(buf, "error", 5)) {
		warn("%s", buf);
		rv = -1;
		goto out;
	}

	rv = 0;
 out:
	close(fd);
	return rv;
}

