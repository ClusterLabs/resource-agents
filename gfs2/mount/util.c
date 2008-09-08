#include "util.h"
#include "libgfscontrol.h"

extern char *prog_name;
extern char *fsname;
extern int verbose;

static int gfs_controld_fd;

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

  /* options used by the mount command only (not in sys/mount.h): */
  { "dirsync",  0, 0, 0  },           /* synchronous directory modifications */
  { "loop",     1, 0, 0  },             /* use a loop device */
  { "auto",     0, 1, 0  },             /* Can be mounted using -a */
  { "noauto",   0, 0, 0  },             /* Can  only be mounted explicitly */
  { "users",    0, 0, 0  },             /* Allow ordinary user to mount */
  { "nousers",  0, 1, 0  },             /* Forbid ordinary user to mount */
  { "user",     0, 0, 0  },             /* Allow ordinary user to mount */
  { "nouser",   0, 1, 0  },             /* Forbid ordinary user to mount */
  { "owner",    0, 0, 0  },             /* Let the owner of the device mount */
  { "noowner",  0, 1, 0  },             /* Device owner has no special privs */
  { "_netdev",  0, 0, 0  },             /* Network device required (netfs) */
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

/* - when unmounting, we don't know the dev and need this function to set it;
   we also want to select the _last_ line with a matching dir since it will
   be the top-most fs that the umount(2) will unmount
   - when mounting, we do know the dev and need this function to use it in the
   comparison (for multiple fs's with the same mountpoint) */

void read_proc_mounts(struct mount_options *mo)
{
	FILE *file;
	char line[PATH_MAX];
	char path[PATH_MAX];
	char type[PATH_MAX];
	char opts[PATH_MAX];
	char device[PATH_MAX];
	char save_line[PATH_MAX];
	char save_opts[PATH_MAX];
	char save_device[PATH_MAX];
	int found = 0;

	file = fopen("/proc/mounts", "r");
	if (!file)
		die("can't open /proc/mounts: %s\n", strerror(errno));

	while (fgets(line, PATH_MAX, file)) {
		if (sscanf(line, "%s %s %s %s", device, path, type, opts) != 4)
			continue;
		if (strcmp(path, mo->dir))
			continue;
		if (mo->dev[0] && strcmp(device, mo->dev))
			continue;
		if (strcmp(type, fsname))
			die("%s is not a %s filesystem\n", mo->dir, fsname);

		/* when there is an input dev specified (mount), we should get
		   only one matching line; when there is no input dev specified
		   (umount), we want the _last_ matching line */

		strncpy(save_device, device, PATH_MAX);
		strncpy(save_opts, opts, PATH_MAX);
		strncpy(save_line, line, PATH_MAX);
		found = 1;
	}

	fclose(file);

	if (!found)
		die("can't find /proc/mounts entry for directory %s\n", mo->dir);
	else {
		strncpy(mo->dev, save_device, PATH_MAX);
		strncpy(mo->opts, save_opts, PATH_MAX);
		strncpy(mo->proc_entry, save_line, PATH_MAX);
	}

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

int lock_dlm_join(struct mount_options *mo, struct gen_sb *sb)
{
	struct gfsc_mount_args ma;
	int fd, rv, result;

	memset(&ma, 0, sizeof(ma));

	strncpy(ma.dir, mo->dir, PATH_MAX);
	strncpy(ma.type, fsname, PATH_MAX);
	strncpy(ma.proto, "lock_dlm", PATH_MAX);
	strncpy(ma.options, mo->opts, PATH_MAX);
	strncpy(ma.dev, mo->dev, PATH_MAX);
	if (mo->locktable[0])
		strncpy(ma.table, mo->locktable, PATH_MAX);
	else
		strncpy(ma.table, sb->locktable, PATH_MAX);

	fd = gfsc_fs_connect();
	if (fd < 0) {
		warn("gfs_controld join connect error: %s", strerror(errno));
		return fd;
	}

	/* tell gfs_controld to join the mountgroup */

	rv = gfsc_fs_join(fd, &ma);
	if (rv < 0) {
		warn("gfs_controld join write error: %s", strerror(errno));
		goto fail;
	}

	/* read the result of the join from gfs_controld */

	rv = gfsc_fs_result(fd, &result, &ma);
	if (rv < 0) {
		warn("gfs_controld result read error: %s", strerror(errno));
		goto fail;
	}

	rv = result;

	switch (rv) {
	case 0:
	case -EALREADY:
		break;

	case -EPROTONOSUPPORT:
		warn("lockproto not supported");
		goto fail;

	case -EOPNOTSUPP:
		warn("jid, first and id are reserved options");
		goto fail;

	case -EBADFD:
		warn("no colon found in table name");
		goto fail;

	case -ENAMETOOLONG:
		warn("fs name too long");
		goto fail;

	case -ESTALE:
		warn("fs is being unmounted");
		goto fail;

	case -EADDRINUSE:
		warn("different fs appears to exist with the same name");
		goto fail;

	case -EBUSY:
		warn("mount point already used or other mount in progress");
		goto fail;

	case -ENOMEM:
		warn("out of memory");
		goto fail;

	case -EBADR:
		warn("fs is for a different cluster");
		goto fail;

	case -ENOANO:
		warn("node not a member of the default fence domain");
		goto fail;

	case -EROFS:
		warn("read-only mount invalid with spectator option");
		goto fail;

	case -EMLINK:
		warn("option string too long");
		goto fail;

	default:
		warn("gfs_controld join error: %d", rv);
		goto fail;
	}

	/*
	 * In addition to the result, gfs_controld also returns
	 * "hostdata=jid=X:id=Y:first=Z" in ma.hostdata.
	 * This is first combined with any hostdata the user gave on
	 * the command line and then the full hostdata is combined
	 * with the "extra" mount otions into the "extra_plus" string.
	 */

	if (strlen(mo->hostdata) + strlen(ma.hostdata) + 1 > PATH_MAX) {
		warn("hostdata too long");
		rv = -1;
		goto fail;
	}

	if (!mo->hostdata[0])
		snprintf(mo->hostdata, PATH_MAX, "%s", ma.hostdata);
	else {
		char *p = strstr(ma.hostdata, "=") + 1;
		strcat(mo->hostdata, ":");
		strcat(mo->hostdata, p);
	}

	log_debug("lock_dlm_join: hostdata: \"%s\"", mo->hostdata);

	if (strlen(mo->extra) == 0)
		snprintf(mo->extra_plus, PATH_MAX, "%s", mo->hostdata);
	else
		snprintf(mo->extra_plus, PATH_MAX, "%s,%s",
			 mo->extra, mo->hostdata);

	/* keep gfs_controld connection open and reuse it below to
	   send the result of mount(2) to gfs_controld, except in
	   the case of another mount (EALREADY) */
	   
	if (rv == -EALREADY)
		gfsc_fs_disconnect(fd);
	else
		gfs_controld_fd = fd;

	return 0;

 fail:
	gfsc_fs_disconnect(fd);
	return rv;
}

void lock_dlm_mount_done(struct mount_options *mo, struct gen_sb *sb,
			 int result)
{
	struct gfsc_mount_args ma;
	int rv;

	if (!gfs_controld_fd)
		return;

	memset(&ma, 0, sizeof(ma));

	strncpy(ma.dir, mo->dir, PATH_MAX);
	strncpy(ma.type, fsname, PATH_MAX);
	strncpy(ma.proto, "lock_dlm", PATH_MAX);
	strncpy(ma.options, mo->opts, PATH_MAX);
	strncpy(ma.dev, mo->dev, PATH_MAX);
	if (mo->locktable[0])
		strncpy(ma.table, mo->locktable, PATH_MAX);
	else
		strncpy(ma.table, sb->locktable, PATH_MAX);

	/* tell gfs_controld the result of mount(2) */

	rv = gfsc_fs_mount_done(gfs_controld_fd, &ma, result);
	if (rv)
		warn("gfs_controld mount_done write error: %s", strerror(errno));

	gfsc_fs_disconnect(gfs_controld_fd);
}

int lock_dlm_leave(struct mount_options *mo, struct gen_sb *sb, int mnterr)
{
	struct gfsc_mount_args ma;
	int rv;

	memset(&ma, 0, sizeof(ma));

	strncpy(ma.dir, mo->dir, PATH_MAX);
	strncpy(ma.type, fsname, PATH_MAX);
	if (mo->locktable[0])
		strncpy(ma.table, mo->locktable, PATH_MAX);
	else
		strncpy(ma.table, sb->locktable, PATH_MAX);

	rv = gfsc_fs_leave(&ma, mnterr);
	if (rv)
		warn("leave: gfs_controld leave error: %s", strerror(errno));

	return rv;
}

int lock_dlm_remount(struct mount_options *mo, struct gen_sb *sb)
{
	struct gfsc_mount_args ma;
	char *mode;
	int fd, rv, result;

	memset(&ma, 0, sizeof(ma));

	/* FIXME: how to check for spectator remounts, we want
	   to disallow remount to/from spectator */

	if (mo->flags & MS_RDONLY)
		mode = "ro";
	else
		mode = "rw";

	strncpy(ma.dir, mo->dir, PATH_MAX);
	strncpy(ma.type, fsname, PATH_MAX);
	strncpy(ma.options, mode, PATH_MAX);

	fd = gfsc_fs_connect();
	if (fd < 0) {
		warn("gfs_controld remount connect error: %s", strerror(errno));
		return fd;
	}

	/* tell gfs_controld about the new mount options */

	rv = gfsc_fs_remount(fd, &ma);
	if (rv) {
		warn("gfs_controld remount write error: %s", strerror(errno));
		goto out;
	}

	/* read the result of the remount from gfs_controld */

	rv = gfsc_fs_result(fd, &result, &ma);
	if (rv < 0) {
		warn("gfs_controld result read error: %s", strerror(errno));
		goto out;
	}

	rv = result;
	if (rv)
		warn("remount not allowed from gfs_controld");
 out:
	gfsc_fs_disconnect(fd);
	return rv;
}

