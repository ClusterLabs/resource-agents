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
#include "util.h"

int
check_type(char *data, unsigned int type)
{
	struct gfs2_meta_header mh;

	gfs2_meta_header_in(&mh, data);
	if (mh.mh_magic != GFS2_MAGIC ||
	    mh.mh_type != type) {
		fprintf(stderr, "%s: expected metadata type %u\n",
			prog_name, type);
		return -1;
	}
	return 0;
}

/**
 * recursive_scan - call a function for each block pointer in a file
 * @di:
 * @height:
 * @bn:
 * @pc:
 * @opaque:
 *
 */

void
recursive_scan(struct gfs2_dinode *di,
	       unsigned int height, uint64_t bn,
	       pointer_call_t pc, void *opaque)
{
	char *data = NULL;
	uint64_t *top, *bottom;
	uint64_t x;
	
	if (!height) {
		data = get_block(di->di_num.no_addr, TRUE);

		top = (uint64_t *)(data + sizeof(struct gfs2_dinode));
		bottom = (uint64_t *)(data + sizeof(struct gfs2_dinode)) + sd_diptrs;
	} else {
		data = get_block(bn, FALSE);
		if (!data)
			return;
		if (check_type(data, GFS2_METATYPE_IN))
			return;

		top = (uint64_t *)(data + sizeof(struct gfs2_meta_header));
		bottom = (uint64_t *)(data + sizeof(struct gfs2_meta_header)) + sd_inptrs;
	}

	for ( ; top < bottom; top++) {
		x = le64_to_cpu(*top);

		pc(di, height, x, opaque);

		if (x && height < di->di_height - 1)
			recursive_scan(di,
				       height + 1, x,
				       pc, opaque);
	}

	free(data);
}

void
foreach_leaf(struct gfs2_dinode *di,
	     leaf_call_t lc, void *opaque)
{
	char *data;
	struct gfs2_leaf leaf;
	uint32_t hsize, len;
	uint32_t ht_offset, lp_offset, ht_offset_cur = -1;
	uint32_t index = 0;
	uint64_t lp[sd_hash_ptrs];
	uint64_t leaf_no;
	int error;

	hsize = 1 << di->di_depth;
	if (hsize * sizeof(uint64_t) != di->di_size)
		die("bad hash table size\n");

	while (index < hsize) {
		lp_offset = index % sd_hash_ptrs;
		ht_offset = index - lp_offset;

		if (ht_offset_cur != ht_offset) {
			error = gfs2_readi(di, (char *)lp, ht_offset * sizeof(uint64_t), sd_hash_bsize);
			if (error != sd_hash_bsize)
				die("FixMe!!!\n");
			ht_offset_cur = ht_offset;
		}

		leaf_no = le64_to_cpu(lp[lp_offset]);
		if (!leaf_no)
			die("NULL leaf pointer\n");

		data = get_block(leaf_no, TRUE);
		gfs2_leaf_in(&leaf, data);
		len = 1 << (di->di_depth - leaf.lf_depth);

		lc(di, data, index, len, leaf_no, opaque);

		free(data);
		index += len;
	}

	if (index != hsize)
		die("screwed up directory\n");
}

static unsigned int
calc_tree_height(struct gfs2_dinode *di, uint64_t size)
{
	uint64_t *arr;
	unsigned int max, height;

	if (di->di_size > size)
		size = di->di_size;

	if (di->di_flags & GFS2_DIF_JDATA) {
		arr = sd_jheightsize;
		max = sd_max_jheight;
	} else {
		arr = sd_heightsize;
		max = sd_max_height;
	}

	for (height = 0; height < max; height++)
		if (arr[height] >= size)
			break;

	return height;
}

struct metapath {
	unsigned int mp_list[GFS2_MAX_META_HEIGHT];
};

static struct metapath *
find_metapath(struct gfs2_dinode *di, uint64_t block)
{
	struct metapath *mp;
	uint64_t b = block;
	unsigned int i;

	mp = malloc(sizeof(struct metapath));
	if (!mp)
		die("out of memory (%s, %u)\n",
		    __FILE__, __LINE__);
	memset(mp, 0, sizeof(struct metapath));

	for (i = di->di_height; i--;) {
		mp->mp_list[i] = b % sd_inptrs;
		b /= sd_inptrs;
	}

	return mp;
}

static uint64_t
lookup_block(char *data, unsigned int height,
	     struct metapath *mp)
{
	unsigned int head_size;
	uint64_t block;

	head_size = (height > 0) ?
		sizeof(struct gfs2_meta_header) : sizeof(struct gfs2_dinode);
	block = *(((uint64_t *)(data + head_size)) + mp->mp_list[height]);

	if (block)
		return le64_to_cpu(block);
	else
		return 0;
}

int
gfs2_block_map(struct gfs2_dinode *di,
	      uint64_t lblock, uint64_t *dblock)
{
	unsigned int bsize;
	unsigned int height;
	struct metapath *mp;
	unsigned int end_of_metadata;
	char *data;
	unsigned int x;
	int error = 0;

	*dblock = 0;

	if (!di->di_height) {
		if (!lblock)
			*dblock = di->di_num.no_addr;
		return 0;
	}

	bsize = (di->di_flags & GFS2_DIF_JDATA) ? sd_jbsize : block_size;

	height = calc_tree_height(di, (lblock + 1) * bsize);
	if (di->di_height < height)
		return 0;

	mp = find_metapath(di, lblock);
	end_of_metadata = di->di_height - 1;

	data = get_block(di->di_num.no_addr, TRUE);

	for (x = 0; x < end_of_metadata; x++) {
		*dblock = lookup_block(data, x, mp);
		free(data);
		if (!*dblock)
			goto out;

		data = get_block(*dblock, FALSE);
		if (!data) {
			error = -1;
			goto out;
		}
	}

	*dblock = lookup_block(data, x, mp);

	free(data);

 out:
	free(mp);

	return error;
}

static int
copy2mem(char *data, void **buf,
	     unsigned int offset, unsigned int size)
{
	char **p = (char **)buf;

	if (data)
		memcpy(*p, data + offset, size);
	else
		memset(*p, 0, size);

	*p += size;

	return 0;
}

int
gfs2_readi(struct gfs2_dinode *di, void *buf,
	  uint64_t offset, unsigned int size)
{
	int journaled = (di->di_flags & GFS2_DIF_JDATA);
	uint64_t lblock, dblock;
	unsigned int o;
	unsigned int amount;
	char *data;
	int copied = 0;
	int error = 0;

	if (offset >= di->di_size)
		return 0;

	if ((offset + size) > di->di_size)
		size = di->di_size - offset;

	if (!size)
		return 0;

	if (journaled) {
		lblock = offset / sd_jbsize;
		o = offset % sd_jbsize;
	} else {
		lblock = offset >> block_size_shift;
		o = offset & (block_size - 1);
	}

	if (!di->di_height)
		o += sizeof(struct gfs2_dinode);
	else if (journaled)
		o += sizeof(struct gfs2_meta_header);

	while (copied < size) {
		amount = size - copied;
		if (amount > block_size - o)
			amount = block_size - o;

		error = gfs2_block_map(di, lblock, &dblock);
		if (error)
			goto fail;

		if (dblock) {
			data = get_block(dblock, FALSE);
			if (!data) {
				error = -1;
				goto fail;
			}
		} else
			data = NULL;

		copy2mem(data, &buf, o, amount);

		if (data)
			free(data);

		copied += amount;
		lblock++;

		o = (journaled) ? sizeof(struct gfs2_meta_header) : 0;
	}

	return copied;

 fail:
	return (copied) ? copied : error;
}

