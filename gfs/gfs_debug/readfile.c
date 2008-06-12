#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <stdint.h>
#include <inttypes.h>

#include "gfs_ondisk.h"
#include "linux_endian.h"

#include "gfs_debug.h"
#include "basic.h"
#include "block_device.h"
#include "readfile.h"
#include "util.h"

void
print_jindex(void)
{
	struct gfs_sb sb;
	struct gfs_dinode di;
	struct gfs_jindex ji;
	char *data;
	char buf[sizeof(struct gfs_jindex)];
	uint64_t o;
	unsigned int x;
	int error;

	must_be_gfs();

	data = get_block(GFS_SB_ADDR * GFS_BASIC_BLOCK / block_size, TRUE);
	gfs_sb_in(&sb, data);
	free(data);

	data = get_block(sb.sb_jindex_di.no_addr, TRUE);
	gfs_dinode_in(&di, data);
	free(data);

	if (di.di_size % sizeof(struct gfs_jindex))
		fprintf(stderr, "%s: strange size for journal index %"PRIu64"\n",
			prog_name, di.di_size);

	for (o = 0, x = 0;; o += sizeof(struct gfs_jindex), x++) {
		error = gfs_readi(&di, buf, o, sizeof(struct gfs_jindex));
		if (!error)
			break;
		if (error < sizeof(struct gfs_jindex))
			continue;
		gfs_jindex_in(&ji, buf);
		printf("Journal %u:\n", x);
		gfs_jindex_print(&ji);
		printf("\n");
	}
}

void
print_rindex(void)
{
	struct gfs_sb sb;
	struct gfs_dinode di;
	struct gfs_rindex ri;
	char *data;
	char buf[sizeof(struct gfs_rindex)];
	uint64_t o;
	unsigned int x;
	int error;

	must_be_gfs();

	data = get_block(GFS_SB_ADDR * GFS_BASIC_BLOCK / block_size, TRUE);
	gfs_sb_in(&sb, data);
	free(data);

	data = get_block(sb.sb_rindex_di.no_addr, TRUE);
	gfs_dinode_in(&di, data);
	free(data);

	if (di.di_size % sizeof(struct gfs_rindex))
		fprintf(stderr, "%s: strange size for resource index %"PRIu64"\n",
			prog_name, di.di_size);

	for (o = 0, x = 0;; o += sizeof(struct gfs_rindex), x++) {
		error = gfs_readi(&di, buf, o, sizeof(struct gfs_rindex));
		if (!error)
			break;
		if (error < sizeof(struct gfs_rindex))
			continue;
		gfs_rindex_in(&ri, buf);
		printf("Resource Group %u:\n", x);
		gfs_rindex_print(&ri);
		printf("\n");
	}
}

void
print_quota(void)
{
	struct gfs_sb sb;
	struct gfs_dinode di;
	struct gfs_quota qu;
	char *data;
	char buf[sizeof(struct gfs_quota)];
	uint64_t o;
	unsigned int x;
	int error;

	must_be_gfs();

	data = get_block(GFS_SB_ADDR * GFS_BASIC_BLOCK / block_size, TRUE);
	gfs_sb_in(&sb, data);
	free(data);

	data = get_block(sb.sb_quota_di.no_addr, TRUE);
	gfs_dinode_in(&di, data);
	free(data);

	for (o = 0, x = 0;; o += sizeof(struct gfs_quota), x++) {
		error = gfs_readi(&di, buf, o, sizeof(struct gfs_quota));
		if (!error)
			break;
		if (error < 0)
			continue;
		gfs_quota_in(&qu, buf);

		if (!qu.qu_limit && !qu.qu_warn && !qu.qu_value)
			continue;

		printf("Quota (%s, %u):\n", (x % 2) ? "group" : "user", x / 2);
		gfs_quota_print(&qu);
		printf("\n");
	}
}

void
print_root(void)
{
	struct gfs_sb sb;
	char *data;

	must_be_gfs();

	data = get_block(GFS_SB_ADDR * GFS_BASIC_BLOCK / block_size, TRUE);
	gfs_sb_in(&sb, data);
	free(data);

	block_number = sb.sb_root_di.no_addr;

	readdir();
}

#define CHUNKSIZE (65536)

void
readfile(void)
{
	struct gfs_dinode di;
	char *data;
	char buf[CHUNKSIZE];
	uint64_t o = 0;
	int error, rc;

	must_be_gfs();

	data = get_block(block_number, TRUE);
	gfs_dinode_in(&di, data);
	free(data);

	if (di.di_header.mh_magic != GFS_MAGIC ||
	    di.di_header.mh_type != GFS_METATYPE_DI)
		die("block %"PRIu64" isn't an inode\n",
		    block_number);

	if (di.di_type != GFS_FILE_REG)
		die("block %"PRIu64" isn't a regular file\n",
		    block_number);

	for (;;) {
		error = gfs_readi(&di, buf, o, CHUNKSIZE);
		if (error <= 0)
			break;
		rc = write(STDOUT_FILENO, buf, error);
		o += error;
	}
}

static void
do_readdir(struct gfs_dinode *di, char *data,
	   uint32_t index, uint32_t len, uint64_t leaf_no,
	   void *opaque)
{
	struct gfs_leaf leaf;

	print_dirents(data, sizeof(struct gfs_leaf));
	gfs_leaf_in(&leaf, data);

	while (leaf.lf_next) {
		data = get_block(leaf.lf_next, FALSE);
		if (!data)
			return;
		print_dirents(data, sizeof(struct gfs_leaf));
		gfs_leaf_in(&leaf, data);
		free(data);
	}
}

void
readdir(void)
{
	struct gfs_dinode di;
	char *data;

	must_be_gfs();

	data = get_block(block_number, TRUE);
	gfs_dinode_in(&di, data);

	if (di.di_header.mh_magic != GFS_MAGIC ||
	    di.di_header.mh_type != GFS_METATYPE_DI)
		die("block %"PRIu64" isn't an inode\n",
		    block_number);

	if (di.di_type != GFS_FILE_DIR)
		die("block %"PRIu64" isn't a directory\n",
		    block_number);

	if (di.di_flags & GFS_DIF_EXHASH)
		foreach_leaf(&di, do_readdir, NULL);
	else
		print_dirents(data, sizeof(struct gfs_dinode));

	free(data);
}
