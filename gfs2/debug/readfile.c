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

#include <linux/gfs2_ondisk.h>
#include "linux_endian.h"

#include "gfs2_debug.h"
#include "basic.h"
#include "block_device.h"
#include "readfile.h"
#include "util.h"

void
print_jindex(void)
{
	printf("FixMe!!!\n");
}

void
print_rindex(void)
{
#if 0
	struct gfs2_sb sb;
	struct gfs2_dinode di;
	struct gfs2_rindex ri;
	char *data;
	char buf[sizeof(struct gfs2_rindex)];
	uint64_t o;
	unsigned int x;
	int error;

	must_be_gfs2();

	data = get_block(GFS2_SB_ADDR * GFS2_BASIC_BLOCK / block_size, TRUE);
	gfs2_sb_in(&sb, data);
	free(data);

	data = get_block(sb.sb_rindex_di.no_addr, TRUE);
	gfs2_dinode_in(&di, data);
	free(data);

	if (di.di_size % sizeof(struct gfs2_rindex))
		fprintf(stderr, "%s: strange size for resource index %"PRIu64"\n",
			prog_name, di.di_size);

	for (o = 0, x = 0;; o += sizeof(struct gfs2_rindex), x++) {
		error = gfs2_readi(&di, buf, o, sizeof(struct gfs2_rindex));
		if (!error)
			break;
		if (error < sizeof(struct gfs2_rindex))
			continue;
		gfs2_rindex_in(&ri, buf);
		printf("Resource Group %u:\n", x);
		gfs2_rindex_print(&ri);
		printf("\n");
	}
#else
	printf("FixMe!!!\n");
#endif
}

void
print_quota(void)
{
#if 0
	struct gfs2_sb sb;
	struct gfs2_dinode di;
	struct gfs2_quota qu;
	char *data;
	char buf[sizeof(struct gfs2_quota)];
	uint64_t o;
	unsigned int x;
	int error;

	must_be_gfs2();

	data = get_block(GFS2_SB_ADDR * GFS2_BASIC_BLOCK / block_size, TRUE);
	gfs2_sb_in(&sb, data);
	free(data);

	data = get_block(sb.sb_quota_di.no_addr, TRUE);
	gfs2_dinode_in(&di, data);
	free(data);

	for (o = 0, x = 0;; o += sizeof(struct gfs2_quota), x++) {
		error = gfs2_readi(&di, buf, o, sizeof(struct gfs2_quota));
		if (!error)
			break;
		if (error < 0)
			continue;
		gfs2_quota_in(&qu, buf);

		if (!qu.qu_limit && !qu.qu_warn && !qu.qu_value)
			continue;

		printf("Quota (%s, %u):\n", (x % 2) ? "group" : "user", x / 2);
		gfs2_quota_print(&qu);
		printf("\n");
	}
#else
	printf("FixMe!!!\n");
#endif
}

void
print_root(void)
{
#if 0
	struct gfs2_sb sb;
	char *data;

	must_be_gfs2();

	data = get_block(GFS2_SB_ADDR * GFS2_BASIC_BLOCK / block_size, TRUE);
	gfs2_sb_in(&sb, data);
	free(data);

	block_number = sb.sb_root_di.no_addr;

	readdir();
#else
	printf("FixMe!!!\n");
#endif
}

#define CHUNKSIZE (65536)

void
readfile(void)
{
	struct gfs2_dinode di;
	char *data;
	char buf[CHUNKSIZE];
	uint64_t o = 0;
	int error;

	must_be_gfs2();

	data = get_block(block_number, TRUE);
	gfs2_dinode_in(&di, data);
	free(data);

	if (di.di_header.mh_magic != GFS2_MAGIC ||
	    di.di_header.mh_type != GFS2_METATYPE_DI)
		die("block %"PRIu64" isn't an inode\n",
		    block_number);

	if (!S_ISREG(di.di_mode))
		die("block %"PRIu64" isn't a regular file\n",
		    block_number);

	for (;;) {
		error = gfs2_readi(&di, buf, o, CHUNKSIZE);
		if (error <= 0)
			break;
		write(STDOUT_FILENO, buf, error);
		o += error;
	}
}

static void
do_readdir(struct gfs2_dinode *di, char *data,
	   uint32_t index, uint32_t len, uint64_t leaf_no,
	   void *opaque)
{
	struct gfs2_leaf leaf;

	print_dirents(data, sizeof(struct gfs2_leaf));
	gfs2_leaf_in(&leaf, data);

	while (leaf.lf_next) {
		data = get_block(leaf.lf_next, FALSE);
		if (!data)
			return;
		print_dirents(data, sizeof(struct gfs2_leaf));
		gfs2_leaf_in(&leaf, data);
		free(data);
	}
}

void
readdir(void)
{
	struct gfs2_dinode di;
	char *data;

	must_be_gfs2();

	data = get_block(block_number, TRUE);
	gfs2_dinode_in(&di, data);

	if (di.di_header.mh_magic != GFS2_MAGIC ||
	    di.di_header.mh_type != GFS2_METATYPE_DI)
		die("block %"PRIu64" isn't an inode\n",
		    block_number);

	if (!S_ISDIR(di.di_mode))
		die("block %"PRIu64" isn't a directory\n",
		    block_number);

	if (di.di_flags & GFS2_DIF_EXHASH)
		foreach_leaf(&di, do_readdir, NULL);
	else
		print_dirents(data, sizeof(struct gfs2_dinode));

	free(data);
}
