#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <time.h>
#include <dirent.h>
#include <limits.h>
#include <stdint.h>
#include <inttypes.h>

#define __user
#include "osi_list.h"

#include "gfs2_quota.h"

struct values {
	osi_list_t v_list;

	uint32_t v_id;
	int64_t v_blocks;
};
typedef struct values values_t;

struct hardlinks {
	osi_list_t hl_list;

	ino_t hl_ino;
};
typedef struct hardlinks hardlinks_t;

/**
 * add_value - add a ID / Allocated Blocks pair to the list
 * @list: the list
 * @id: the ID number
 * @blocks: the number of blocks to add
 *
 */

static void
add_value(osi_list_t *list, uint32_t id, int64_t blocks)
{
	osi_list_t *tmp;
	values_t *v;

	for (tmp = list->next; tmp != list; tmp = tmp->next) {
		v = osi_list_entry(tmp, values_t, v_list);
		if (v->v_id != id)
			continue;

		v->v_blocks += blocks;

		osi_list_del(&v->v_list);
		osi_list_add(&v->v_list, list);

		return;
	}

	type_zalloc(v, values_t, 1);

	v->v_id = id;
	v->v_blocks = blocks;

	osi_list_add(&v->v_list, list);
}

/**
 * test_and_add_hard_link - Add a inode that has hard links to the list
 * @list: the list of inodes with hard links
 * @ino: the number of the inode to add
 *
 * Returns: Returns TRUE if the inode was already on the list, FALSE if it wasn't
 */

static int
test_and_add_hard_link(osi_list_t *list, ino_t ino)
{
	osi_list_t *tmp;
	hardlinks_t *hl;

	for (tmp = list->next; tmp != list; tmp = tmp->next) {
		hl = osi_list_entry(tmp, hardlinks_t, hl_list);
		if (hl->hl_ino != ino)
			continue;

		return TRUE;
	}

	type_zalloc(hl, hardlinks_t, 1);

	hl->hl_ino = ino;

	osi_list_add(&hl->hl_list, list);

	return FALSE;
}

/**
 * scan_fs - recursively scan a filesystem and figure out what IDs have what
 * @device: the device the filesystem is on
 * @dirname: the name of the directory to read
 * @uid: returned list of UIDs for this FS
 * @gid: returned list of GIDs for this FS
 * @hl: returned list of hard links for this FS
 *
 */

static void
scan_fs(dev_t device, char *dirname,
	osi_list_t *uid, osi_list_t *gid, osi_list_t *hl)
{
	DIR *dir;
	struct dirent *de;
	struct stat st;
	char *name;
	int error;

	dir = opendir(dirname);
	if (!dir)
		die("can't open directory %s: %s\n", dirname, strerror(errno));

	while ((de = readdir(dir))) {
		if (strcmp(de->d_name, "..") == 0)
			continue;

		type_alloc(name, char,
			   strlen(dirname) + strlen(de->d_name) + 2);
		if (dirname[strlen(dirname) - 1] == '/')
			sprintf(name, "%s%s", dirname, de->d_name);
		else
			sprintf(name, "%s/%s", dirname, de->d_name);

		error = lstat(name, &st);
		if (error)
			die("can't stat file %s: %s\n", name, strerror(errno));

		if (st.st_dev != device)
			die("umount %s and try again\n", name);

		if (S_ISDIR(st.st_mode)) {
			if (strcmp(de->d_name, ".") == 0) {
				add_value(uid, st.st_uid, st.st_blocks);
				add_value(gid, st.st_gid, st.st_blocks);
			} else
				scan_fs(device, name, uid, gid, hl);
		} else if (st.st_nlink == 1 ||
			   !test_and_add_hard_link(hl, st.st_ino)) {
			add_value(uid, st.st_uid, st.st_blocks);
			add_value(gid, st.st_gid, st.st_blocks);
		}

		free(name);
	}

	closedir(dir);
}

/**
 * read_quota_file - read the quota file and return list of its contents
 * @comline: the command line arguments
 * @uid: returned list of UIDs for the filesystem
 * @gid: returned list of GIDs for the filesystem
 *
 */
static void
read_quota_file(struct gfs2_sbd *sdp, commandline_t *comline,
		osi_list_t *uid, osi_list_t *gid)
{
	int fd;
	char buf[sizeof(struct gfs2_quota)];
	struct gfs2_quota q;
	uint64_t offset = 0;
	uint32_t id, prev, maxid;
	int error, pass, id_type;
	char quota_file[BUF_SIZE];
	
	strcpy(sdp->path_name, comline->filesystem);
	check_for_gfs2(sdp);
	read_superblock(&sdp->sd_sb, sdp);
	if (!find_gfs2_meta(sdp))
		mount_gfs2_meta(sdp);
	lock_for_admin(sdp);
	
	strcpy(quota_file, sdp->metafs_path);
	strcat(quota_file, "/quota");

	fd = open(quota_file, O_RDONLY);
	if (fd < 0) {
		close(sdp->metafs_fd);
		cleanup_metafs(sdp);
		die("can't open file %s: %s\n", comline->filesystem,
		    strerror(errno));
	}

	if (!is_valid_quota_list(fd)) {
		print_quota_list_warning();
		goto do_old_school;
	}
	get_last_quota_id(fd, &maxid);
	
	for (pass=0; pass<2; pass++) {
		id = 0;
		id_type = pass ? GQ_ID_GROUP : GQ_ID_USER;
		
		do {
			read_quota_internal(fd, id, id_type, &q);
			prev = id;
			q.qu_value <<= sdp->sd_sb.sb_bsize_shift - 9;
			
			if (q.qu_value) {
				if (pass)
					add_value(gid, id, q.qu_value);
				else
					add_value(uid, id, q.qu_value);
			}
			id = q.qu_ll_next;
		} while(id && id > prev && id <= maxid);
	}
	goto out;
	
do_old_school:
	do {
		
		memset(buf, 0, sizeof(struct gfs2_quota));
		/* read hidden quota file here */
		lseek(fd, offset, SEEK_SET);
		error = read(fd, buf, sizeof(struct gfs2_quota));
		if (error < 0) {
			close(fd);
			close(sdp->metafs_fd);
			cleanup_metafs(sdp);
			die("can't read quota file (%d): %s\n",
			    error, strerror(errno));
		}
		gfs2_quota_in(&q, buf);

		id = (offset / sizeof(struct gfs2_quota)) >> 1;
		q.qu_value <<= sdp->sd_sb.sb_bsize_shift - 9;

		if (q.qu_value) {
			if (id * sizeof(struct gfs2_quota) * 2 == offset)
				add_value(uid, id, q.qu_value);
			else
				add_value(gid, id, q.qu_value);
		}

		offset += sizeof(struct gfs2_quota);
	} while (error == sizeof(struct gfs2_quota));

out:
	close(fd);
	close(sdp->metafs_fd);
	cleanup_metafs(sdp);
}

/**
 * print_list - print the contents of an ID list
 * @str: a string describing the list
 * @list: the list
 *
 */

static void
print_list(char *str, osi_list_t *list)
{
#if 0
	osi_list_t *tmp;
	values_t *v;

	for (tmp = list->next; tmp != list; tmp = tmp->next) {
		v = osi_list_entry(tmp, values_t, v_list);
		printf("%s %10u: %"PRId64"\n", str, v->v_id, v->v_blocks);
	}
#endif
}

/**
 * do_compare - compare to ID lists and see if they match
 * @type: the type of list (UID or GID)
 * @fs_list: the list derived from scaning the FS
 * @qf_list: the list derived from reading the quota file
 *
 * Returns: TRUE if there was a mismatch
 */

static int
do_compare(char *type, osi_list_t *fs_list, osi_list_t *qf_list)
{
	osi_list_t *tmp1, *tmp2;
	values_t *v1, *v2;
	int found;
	int mismatch = FALSE;

	for (tmp1 = fs_list->next; tmp1 != fs_list; tmp1 = tmp1->next) {
		v1 = osi_list_entry(tmp1, values_t, v_list);

		found = FALSE;

		for (tmp2 = qf_list->next; tmp2 != qf_list; tmp2 = tmp2->next) {
			v2 = osi_list_entry(tmp2, values_t, v_list);
			if (v1->v_id != v2->v_id)
				continue;

			if (v1->v_blocks != v2->v_blocks) {
				printf("mismatch: %s %u: scan = %"PRId64", quotafile = %"PRId64"\n",
				       type, v1->v_id,
				       v1->v_blocks, v2->v_blocks);
				mismatch = TRUE;
			}

			osi_list_del(&v2->v_list);
			free(v2);

			found = TRUE;
			break;
		}

		if (!found) {
			printf("mismatch: %s %u: scan = %"PRId64", quotafile = %"PRId64"\n",
			       type, v1->v_id,
			       v1->v_blocks, (int64_t)0);
			mismatch = TRUE;
		}
	}

	for (tmp2 = qf_list->next; tmp2 != qf_list; tmp2 = tmp2->next) {
		v2 = osi_list_entry(tmp2, values_t, v_list);

		printf("mismatch: %s %u: scan = %"PRId64", quotafile = %"PRId64"\n",
		       type, v2->v_id,
		       (int64_t)0, v2->v_blocks);
		mismatch = TRUE;
	}

	return mismatch;
}

/**
 * verify_pathname - make sure the path on the command line is a mount point
 * @comline: the command line arguments
 *
 * Returns: the device the filesystem is on
 */

static dev_t
verify_pathname(commandline_t *comline)
{
	struct stat st1, st2;
	dev_t device;
	char *name;
	int error;

	if (!*comline->filesystem)
		die("need a filesystem to work on\n");

	error = lstat(comline->filesystem, &st1);
	if (error)
		die("can't stat %s: %s\n", comline->filesystem,
		    strerror(errno));

	if (!S_ISDIR(st1.st_mode))
		die("%s must be a directory\n", comline->filesystem);

	device = st1.st_dev;

	for (;;) {
		type_alloc(name, char, strlen(comline->filesystem) + 4);
		sprintf(name, "%s/..", comline->filesystem);

		error = lstat(name, &st2);
		if (error)
			die("can't stat %s: %s\n", name, strerror(errno));

		if (st2.st_dev != device || st2.st_ino == st1.st_ino) {
			free(name);
			break;
		}

		if (!realpath(name, comline->filesystem))
			die("error resolving filesystem pathname: %s\n",
			    strerror(errno));

		free(name);

		st1 = st2;
	}

	return device;
}

/**
 * do_check - Check what's in the quota file
 * @comline: the struct containing the parsed command line arguments
 *
 */

void
do_check(struct gfs2_sbd *sdp, commandline_t *comline)
{
	dev_t device;
	osi_list_t fs_uid, fs_gid, qf_uid, qf_gid;
	osi_list_t hl;
	int mismatch;

	osi_list_init(&fs_uid);
	osi_list_init(&fs_gid);
	osi_list_init(&qf_uid);
	osi_list_init(&qf_gid);
	osi_list_init(&hl);

	device = verify_pathname(comline);

	scan_fs(device, comline->filesystem, &fs_uid, &fs_gid, &hl);
	read_quota_file(sdp, comline, &qf_uid, &qf_gid);

	print_list("fs user ", &fs_uid);
	print_list("fs group", &fs_gid);
	print_list("qf user ", &qf_uid);
	print_list("qf group", &qf_gid);

	mismatch = do_compare("user", &fs_uid, &qf_uid);
	mismatch |= do_compare("group", &fs_gid, &qf_gid);

	if (mismatch)
		exit(EXIT_FAILURE);
}

/**
 * set_list - write a list of IDs into the quota file
 * @comline: the command line arguments
 * @user: TRUE if this is a list of UIDs, FALSE if it is a list of GIDs
 * @list: the list of IDs and block counts
 * @multiplier: multiply block counts by this
 *
 */

static void
set_list(struct gfs2_sbd *sdp, commandline_t *comline, int user, 
	 osi_list_t *list, int64_t multiplier)
{
	int fd;
	osi_list_t *tmp;
	values_t *v;
	uint64_t offset;
	int64_t value;
	int error;
	char quota_file[BUF_SIZE];
	char id_str[16];
	char *fs;

	strcpy(sdp->path_name, comline->filesystem);
	check_for_gfs2(sdp);
	read_superblock(&sdp->sd_sb, sdp);
	if (!find_gfs2_meta(sdp))
		mount_gfs2_meta(sdp);
	lock_for_admin(sdp);
	
	strcpy(quota_file, sdp->metafs_path);
	strcat(quota_file, "/quota");

	fd = open(quota_file, O_WRONLY);
	if (fd < 0) {
		close(sdp->metafs_fd);
		cleanup_metafs(sdp);
		die("can't open file %s: %s\n", comline->filesystem,
		    strerror(errno));
	}

	for (tmp = list->next; tmp != list; tmp = tmp->next) {
		v = osi_list_entry(tmp, values_t, v_list);

		offset = (2 * (uint64_t)v->v_id + ((user) ? 0 : 1)) *
			sizeof(struct gfs2_quota);
		offset += (unsigned long)(&((struct gfs2_quota *)NULL)->qu_value);

		value = v->v_blocks * multiplier;
		value >>= sdp->sd_sb.sb_bsize_shift - 9;
		value = cpu_to_be64(value);

		lseek(fd, offset, SEEK_SET);
		error = write(fd, (char*)&value, sizeof(uint64_t));
		if (error != sizeof(uint64_t)) {
			fprintf(stderr, "can't write quota file (%d): %s\n",
			    error, strerror(errno));
			goto out;
		}

		/* Write the id to sysfs quota refresh file to refresh gfs quotas */
		fs = mp2fsname(comline->filesystem);
		sprintf(id_str, "%d", comline->id);
		set_sysfs(fs, (user) ? "quota_refresh_user" :
			  "quota_refresh_group", id_str);
	}

out:
	close(fd);
	close(sdp->metafs_fd);
	cleanup_metafs(sdp);
}

/**
 * do_quota_init - initialize the quota file
 * @comline: the command line arguments
 *
 */

void
do_quota_init(struct gfs2_sbd *sdp, commandline_t *comline)
{
	dev_t device;
	osi_list_t fs_uid, fs_gid, qf_uid, qf_gid;
	osi_list_t hl;
	values_t *v;

	osi_list_init(&fs_uid);
	osi_list_init(&fs_gid);
	osi_list_init(&qf_uid);
	osi_list_init(&qf_gid);
	osi_list_init(&hl);

	device = verify_pathname(comline);

	scan_fs(device, comline->filesystem, &fs_uid, &fs_gid, &hl);
	read_quota_file(sdp, comline, &qf_uid, &qf_gid);

	type_zalloc(v, values_t, 1);
	v->v_id = 0;
	v->v_blocks = 0;
	osi_list_add(&v->v_list, &qf_uid);

	type_zalloc(v, values_t, 1);
	v->v_id = 0;
	v->v_blocks = 0;
	osi_list_add(&v->v_list, &qf_gid);

	print_list("fs user ", &fs_uid);
	print_list("fs group", &fs_gid);
	print_list("qf user ", &qf_uid);
	print_list("qf group", &qf_gid);

	set_list(sdp, comline, TRUE, &qf_uid, 0);
	set_list(sdp, comline, FALSE, &qf_gid, 0);
	set_list(sdp, comline, TRUE, &fs_uid, 1);
	set_list(sdp, comline, FALSE, &fs_gid, 1);
	
	do_sync(sdp, comline);

	do_check(sdp, comline);
}
