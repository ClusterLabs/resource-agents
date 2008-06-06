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

/**
 * verify_block_size -
 * @bsize:
 *
 */

static int
verify_block_size(unsigned int bsize)
{
	unsigned int x = 512;

	for (;;) {
		if (!x)
			return -1;
		if (x == bsize)
			return 0;
		x <<= 1;
	}
}

/**
 * verify_gfs -
 *
 */

void
verify_gfs(void)
{
	char buf[GFS_BASIC_BLOCK];
	struct gfs_sb sb;

	if (device_size < (GFS_SB_ADDR + 1) * GFS_BASIC_BLOCK)
		return;

	do_lseek(device_fd, GFS_SB_ADDR * GFS_BASIC_BLOCK);
	do_read(device_fd, buf, GFS_BASIC_BLOCK);

	gfs_sb_in(&sb, buf);

	if (sb.sb_header.mh_magic != GFS_MAGIC ||
	    sb.sb_header.mh_type != GFS_METATYPE_SB ||
	    sb.sb_bsize != 1 << sb.sb_bsize_shift ||
	    verify_block_size(sb.sb_bsize))
		return;

	if (!block_size || block_size == sb.sb_bsize) {
		unsigned int x;

		is_gfs = TRUE;
		block_size = sb.sb_bsize;
		block_size_shift = sb.sb_bsize_shift;

		sd_diptrs = (block_size - sizeof(struct gfs_dinode)) / sizeof(uint64_t);
		sd_inptrs = (block_size - sizeof(struct gfs_indirect)) / sizeof(uint64_t);
		sd_jbsize = block_size - sizeof(struct gfs_meta_header);
		sd_hash_bsize = block_size / 2;
		sd_hash_ptrs = sd_hash_bsize / sizeof(uint64_t);

		sd_heightsize[0] = block_size - sizeof(struct gfs_dinode);
		sd_heightsize[1] = block_size * sd_diptrs;
		for (x = 2;; x++) {
			uint64_t space = sd_heightsize[x - 1] * sd_inptrs;
			uint64_t d = space / sd_inptrs;
			uint32_t m = space % sd_inptrs;

			if (d != sd_heightsize[x - 1] || m)
				break;
			sd_heightsize[x] = space;
		}
		sd_max_height = x;

		sd_jheightsize[0] = block_size - sizeof(struct gfs_dinode);
		sd_jheightsize[1] = sd_jbsize * sd_diptrs;
		for (x = 2;; x++) {
			uint64_t space = sd_jheightsize[x - 1] * sd_inptrs;
			uint64_t d = space / sd_inptrs;
			uint32_t m = space % sd_inptrs;

			if (d != sd_jheightsize[x - 1] || m)
				break;
			sd_jheightsize[x] = space;
		}
		sd_max_jheight = x;
	}
}

/**
 * must_be_gfs -
 *
 */

void
must_be_gfs(void)
{
	if (!is_gfs)
		die("not a gfs filesystem\n");
}

/**
 * scan_device -
 *
 */

void
scan_device(void)
{
	char data[GFS_BASIC_BLOCK];
	uint64_t bb;
	struct gfs_meta_header mh;

	for (bb = 0; (bb + 1) * GFS_BASIC_BLOCK <= device_size; bb++) {
		do_lseek(device_fd, bb * GFS_BASIC_BLOCK);
		do_read(device_fd, data, GFS_BASIC_BLOCK);
		gfs_meta_header_in(&mh, data);

		if (mh.mh_magic == GFS_MAGIC &&
		    mh.mh_type && mh.mh_type <= GFS_METATYPE_EA)
			printf("sector %"PRIu64": type %u\n",
			       bb, mh.mh_type);
	}
}

/**
 * print_superblock -
 *
 */

void
print_superblock(void)
{
	char *data;
	struct gfs_sb sb;

	must_be_gfs();

	data = get_block(GFS_SB_ADDR * GFS_BASIC_BLOCK / block_size, TRUE);
	gfs_sb_in(&sb, data);
	free(data);

	gfs_sb_print(&sb);
}

/**
 * print_bitmaps -
 * @data:
 * @offset:
 *
 */

static void
print_bitmaps(char *data, unsigned int offset)
{
	unsigned int bn = 0;
	unsigned int bit;
	unsigned char value;
	char *type;

	printf("\n");

	for (; offset < block_size; offset++) {
		for (bit = 0; bit < GFS_NBBY; bit++) {
			value = data[offset];
			value = (value >> (bit * GFS_BIT_SIZE)) & GFS_BIT_MASK;
			switch (value) {
			case GFS_BLKST_FREE:
				type = "free";
				break;
			case GFS_BLKST_USED:
				type = "used data";
				break;
			case GFS_BLKST_FREEMETA:
				type = "free meta";
				break;
			case GFS_BLKST_USEDMETA:
				type = "used meta";
				break;
			default:
				ASSERT(FALSE,);
			}
			printf("  block %u: %s\n", bn, type);
			bn++;
		}
	}
}

/**
 * print_stuffed_hash -
 * @data:
 *
 */

static void
print_stuffed_hash(char *data)
{
	uint64_t *p = (uint64_t *)(data + sizeof(struct gfs_dinode));
	uint64_t *end = (uint64_t *)(((char *)p) + block_size / 2);
	uint64_t this, last = 0;
	unsigned int run = 0;
	int first = TRUE;

	printf("\n");

	for (; p < end; p++) {
		this = gfs64_to_cpu(*p);

		if (first) {
			first = FALSE;
			run = 1;
		} else {
			if (this == last)
				run++;
			else {
				printf("  pointer: %"PRIu64" (%u)\n",
				       last, run);
				run = 1;
			}
		}
			
		last = this;
	}

	printf("  pointer: %"PRIu64" (%u)\n",
	       last, run);
}

/**
 * print_dirents -
 * @data:
 * @offset:
 *
 * Make this more robust
 *
 */

void
print_dirents(char *data, unsigned int offset)
{
	struct gfs_dirent de;

	for (; offset < block_size; offset += de.de_rec_len) {
		printf("\n");
		gfs_dirent_in(&de, data + offset);

		if (sizeof(struct gfs_dirent) + de.de_name_len > de.de_rec_len)
			continue;
		if (offset + sizeof(struct gfs_dirent) + de.de_name_len > block_size)
			break;
		if (de.de_inum.no_formal_ino)
			gfs_dirent_print(&de, data + offset + sizeof(struct gfs_dirent));
	}
}

/**
 * print_pointers -
 * @data:
 * @offset:
 *
 */

static void
print_pointers(char *data, unsigned int offset)
{
	uint64_t *p = (uint64_t *)(data + offset);
	uint64_t *end = (uint64_t *)(data + block_size);
	unsigned int x = 0;

	printf("\n");

	for (; p < end; p++, x++)
		if (*p)
			printf("  pointer #%u: %"PRIu64"\n",
			       x, gfs64_to_cpu(*p));
}

/**
 * identify_block -
 *
 */

void
identify_block(void)
{
	char *data;
	struct gfs_meta_header mh;

	must_be_gfs();

	data = get_block(block_number, TRUE);
	gfs_meta_header_in(&mh, data);

	if (mh.mh_magic != GFS_MAGIC) {
		printf("Not GFS metadata\n");
		free(data);
		return;
	}

	switch (mh.mh_type) {
	case GFS_METATYPE_NONE:
		printf("GFS_METATYPE_NONE\n");
		break;

	case GFS_METATYPE_SB:
		printf("Super\n");
		if (verbose) {
			struct gfs_sb sb;
			gfs_sb_in(&sb, data);
			gfs_sb_print(&sb);
		}
		break;

	case GFS_METATYPE_RG:
		printf("Resource Group Header\n");
		if (verbose) {
			struct gfs_rgrp rg;
			gfs_rgrp_in(&rg, data);
			gfs_rgrp_print(&rg);
			if (verbose > 1)
				print_bitmaps(data, sizeof(struct gfs_rgrp));
		}
		break;

	case GFS_METATYPE_RB:
		printf("Resource Group Bitmap\n");
		if (verbose) {
			gfs_meta_header_print(&mh);
			if (verbose > 1)
				print_bitmaps(data, sizeof(struct gfs_meta_header));
		}
		break;

	case GFS_METATYPE_DI:
		printf("Dinode\n");
		if (verbose) {
			struct gfs_dinode di;
			gfs_dinode_in(&di, data);
			gfs_dinode_print(&di);
			if (verbose > 1) {
				if (di.di_height)
					print_pointers(data, sizeof(struct gfs_dinode));
				else {
					if (di.di_type == GFS_FILE_REG)
						printf("\n  stuffed data\n");
					else if (di.di_type == GFS_FILE_DIR &&
						(di.di_flags & GFS_DIF_EXHASH))
						print_stuffed_hash(data);
					else if (di.di_type == GFS_FILE_DIR)
						print_dirents(data, sizeof(struct gfs_dinode));
					else if (di.di_type == GFS_FILE_LNK)
						printf("\nsymlink to %s\n",
						       data + sizeof(struct gfs_dinode));
				}
			}
		}
		break;

	case GFS_METATYPE_IN:
		printf("Indirect\n");
		if (verbose) {
			struct gfs_indirect in;
			gfs_indirect_in(&in, data);
			gfs_indirect_print(&in);
			if (verbose > 1) {
				print_pointers(data, sizeof(struct gfs_indirect));
			}
		}
		break;

	case GFS_METATYPE_LF:
		printf("Directory Leaf\n");
		if (verbose) {
			struct gfs_leaf lf;
			gfs_leaf_in(&lf, data);
			gfs_leaf_print(&lf);
			if (verbose > 1)
				print_dirents(data, sizeof(struct gfs_leaf));
		}
		break;

	case GFS_METATYPE_JD:
		printf("Journaled Data\n");
		if (verbose)
			gfs_meta_header_print(&mh);
		break;

	case GFS_METATYPE_LH:
		printf("Log Header\n");
		if (verbose) {
			struct gfs_log_header lh;
			gfs_log_header_in(&lh, data);
			gfs_log_header_print(&lh);
		}
		break;

	case GFS_METATYPE_LD:
		printf("Lock Descriptor\n");
		if (verbose) {
			struct gfs_log_descriptor ld;
			gfs_desc_in(&ld, data);
			gfs_desc_print(&ld);
		}
		break;

	case GFS_METATYPE_EA:
		printf("Extended Attribute\n");
		if (verbose)
			gfs_meta_header_print(&mh);
		break;

	default:
		printf("Unknown metadata type\n");
		if (verbose)
			gfs_meta_header_print(&mh);
		break;
	}

	free(data);
}

