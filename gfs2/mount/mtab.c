#include "util.h"

extern char *prog_name;
extern char *fsname;
extern int verbose;
static int ignoring_mtab;

/* Don't bother with /etc/mtab if:
   - /etc/mtab is a link to /proc/mounts
   - there is no /etc/mtab file
   - we can't write to /etc/mtab */

static int ignore_mtab(void)
{
	struct stat sbuf;
	int fd;

	if (ignoring_mtab)
		return 1;

	if (lstat("/etc/mtab", &sbuf) < 0)
		goto do_ignore;

	if (S_ISLNK(sbuf.st_mode))
		goto do_ignore;

	fd = open("/etc/mtab", O_RDWR, 0644);
	if (fd < 0)
		goto do_ignore;

	close(fd);
	return 0;

 do_ignore:
	ignoring_mtab = 1;
	return 1;
}

/* Whichever process successfully links their own /etc/mtab~pid file to
   /etc/mtab~ gets the lock.  We just sleep/retry until we get the lock.
   This is the locking method used by util-linux/mount/fstab.c which we
   need to keep in sync with. */

static void lock_mtab(void)
{
	char fname[32];
	int rv, fd, e, retries = 0;

	if (ignore_mtab())
		return;

	sprintf(fname, "/etc/mtab~%d", getpid());
 retry:
	fd = open(fname, O_WRONLY|O_CREAT, 0);
	e = errno;
	if (fd < 0)
		die("can't create mtab lock file %s: %s\n", fname, strerror(e));
	close(fd);

	rv = link(fname, "/etc/mtab~");
	e = errno;

	unlink(fname);

	if (rv < 0 && e != EEXIST)
		die("can't link %s to /etc/mtab~: %s\n", fname, strerror(e));

	if (rv < 0) {
		if (++retries > 5)
			warn("waiting to lock /etc/mtab~: try unlinking "
			     "stale /etc/mtab~ file\n");
		sleep(1);
		goto retry;
	}
}

static void unlock_mtab(void)
{
	if (ignore_mtab())
		return;
	unlink("/etc/mtab~");
}

void add_mtab_entry(struct mount_options *mo)
{
	FILE *file;

	read_proc_mounts(mo);

	if (ignore_mtab())
		return;

	lock_mtab();

	file = fopen("/etc/mtab", "a");
	if (!file) {
		warn("can't add entry to /etc/mtab");
		return;
	}

	fprintf(file, "%s", mo->proc_entry);

	fclose(file);

	unlock_mtab();
}

/* This follows what util-linux/mount/fstab.c does wrt writing new mtab.tmp
   and replacing /etc/mtab with it, we need to keep in sync with fstab.c's
   procedure for this. */

void del_mtab_entry(struct mount_options *mo)
{
	char line[PATH_MAX];
	char device[PATH_MAX];
	char path[PATH_MAX];
	char type[16];
	FILE *mtab, *mtmp;
	mode_t old_umask;
	struct stat sbuf;
	int found = 0;

	if (ignore_mtab())
		return;

	lock_mtab();

	old_umask = umask(077);

	mtmp = fopen("/etc/mtab.tmp", "w");
	mtab = fopen("/etc/mtab", "r");

	umask(old_umask);

	if (!mtmp || !mtab)
		goto fail;

	while (fgets(line, PATH_MAX, mtab)) {
		/* exclude the line matching the fs being unmounted
		   from the next version of mtab */

		if ((sscanf(line, "%s %s %s", device, path, type) == 3) &&
		    (strncmp(type, "gfs", 3) == 0) &&
		    (strcmp(path, mo->dir) == 0) &&
		    (strcmp(device, mo->dev) == 0)) {
			found = 1;
			continue;
		}

		/* all other lines from mtab are included in
		   the next version of mtab */

		if (fprintf(mtmp, "%s", line) < 0) {
			int e = errno;
			warn("error writing to /etc/mtab.tmp: %s", strerror(e));
		}
	}

	if (!found) {
		warn("file system mounted on %s not found in mtab", mo->dir);
		goto fail;
	}

	if (fchmod(fileno(mtmp), S_IRUSR|S_IWUSR|S_IRGRP|S_IROTH) < 0) {
		int e = errno;
		warn("error changing mode of /etc/mtab.tmp: %s", strerror(e));
	}

	if (stat("/etc/mtab", &sbuf) < 0) {
		int e = errno;
		warn("error stating /etc/mtab: %s", strerror(e));
	} else
		if(chown("/etc/mtab.tmp", sbuf.st_uid, sbuf.st_gid) < 0) {
			int e = errno;
			warn("error changing owner of /etc/mtab.tmp: %s", strerror(e));
		}

	fclose(mtmp);
	fclose(mtab);

	if (rename("/etc/mtab.tmp", "/etc/mtab") < 0) {
		int e = errno;
		warn("can't rename /etc/mtab.tmp to /etc/mtab: %s",
		     strerror(e));
		goto fail_unlink;
	}

	unlock_mtab();
	return;

 fail:
	fclose(mtmp);
	fclose(mtab);
 fail_unlink:
	unlink("/etc/mtab.tmp");
	unlock_mtab();
}

