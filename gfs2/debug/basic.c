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
 * verify_gfs2 -
 *
 */

void
verify_gfs2(void)
{
	char buf[GFS2_BASIC_BLOCK];
	struct gfs2_sb sb;

	if (device_size < (GFS2_SB_ADDR + 1) * GFS2_BASIC_BLOCK)
		return;

	do_lseek(device_fd, GFS2_SB_ADDR * GFS2_BASIC_BLOCK);
	do_read(device_fd, buf, GFS2_BASIC_BLOCK);

	gfs2_sb_in(&sb, buf);

	if (sb.sb_header.mh_magic != GFS2_MAGIC ||
	    sb.sb_header.mh_type != GFS2_METATYPE_SB ||
	    sb.sb_bsize != 1 << sb.sb_bsize_shift ||
	    verify_block_size(sb.sb_bsize))
		return;

	if (!block_size || block_size == sb.sb_bsize) {
		unsigned int x;

		is_gfs2 = TRUE;
		block_size = sb.sb_bsize;
		block_size_shift = sb.sb_bsize_shift;

		sd_diptrs = (block_size - sizeof(struct gfs2_dinode)) / sizeof(uint64_t);
		sd_inptrs = (block_size - sizeof(struct gfs2_meta_header)) / sizeof(uint64_t);
		sd_jbsize = block_size - sizeof(struct gfs2_meta_header);
		sd_hash_bsize = block_size / 2;
		sd_hash_ptrs = sd_hash_bsize / sizeof(uint64_t);

		sd_heightsize[0] = block_size - sizeof(struct gfs2_dinode);
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

		sd_jheightsize[0] = block_size - sizeof(struct gfs2_dinode);
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
 * must_be_gfs2 -
 *
 */

void
must_be_gfs2(void)
{
	if (!is_gfs2)
		die("not a gfs2 filesystem\n");
}

/**
 * scan_device -
 *
 */

void
scan_device(void)
{
	char data[GFS2_BASIC_BLOCK];
	uint64_t bb;
	struct gfs2_meta_header mh;

	for (bb = 0; (bb + 1) * GFS2_BASIC_BLOCK <= device_size; bb++) {
		do_lseek(device_fd, bb * GFS2_BASIC_BLOCK);
		do_read(device_fd, data, GFS2_BASIC_BLOCK);
		gfs2_meta_header_in(&mh, data);

		if (mh.mh_magic == GFS2_MAGIC &&
		    mh.mh_type && mh.mh_type <= GFS2_METATYPE_EA)
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
	struct gfs2_sb sb;

	must_be_gfs2();

	data = get_block(GFS2_SB_ADDR * GFS2_BASIC_BLOCK / block_size, TRUE);
	gfs2_sb_in(&sb, data);
	free(data);

	gfs2_sb_print(&sb);
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
		for (bit = 0; bit < GFS2_NBBY; bit++) {
			value = data[offset];
			value = (value >> (bit * GFS2_BIT_SIZE)) & GFS2_BIT_MASK;
			switch (value) {
			case GFS2_BLKST_FREE:
				type = "free";
				break;
			case GFS2_BLKST_USED:
				type = "used data";
				break;
			case GFS2_BLKST_INVALID:
				type = "invalid";
				break;
			case GFS2_BLKST_DINODE:
				type = "dinode";
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
	uint64_t *p = (uint64_t *)(data + sizeof(struct gfs2_dinode));
	uint64_t *end = (uint64_t *)(((char *)p) + block_size / 2);
	uint64_t this, last = 0;
	unsigned int run = 0;
	int first = TRUE;

	printf("\n");

	for (; p < end; p++) {
		this = le64_to_cpu(*p);

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
	struct gfs2_dirent de;

	for (; offset < block_size; offset += de.de_rec_len) {
		printf("\n");
		gfs2_dirent_in(&de, data + offset);

		if (sizeof(struct gfs2_dirent) + de.de_name_len > de.de_rec_len)
			continue;
		if (offset + sizeof(struct gfs2_dirent) + de.de_name_len > block_size)
			break;
		if (de.de_inum.no_formal_ino)
			gfs2_dirent_print(&de, data + offset + sizeof(struct gfs2_dirent));
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
			       x, le64_to_cpu(*p));
}

/**
 * identify_block -
 *
 */

void
identify_block(void)
{
	char *data;
	struct gfs2_meta_header mh;

	must_be_gfs2();

	data = get_block(block_number, TRUE);
	gfs2_meta_header_in(&mh, data);

	if (mh.mh_magic != GFS2_MAGIC) {
		printf("Not GFS2 metadata\n");
		free(data);
		return;
	}

	switch (mh.mh_type) {
	case GFS2_METATYPE_NONE:
		printf("GFS2_METATYPE_NONE\n");
		break;

	case GFS2_METATYPE_SB:
		printf("Super\n");
		if (verbose) {
			struct gfs2_sb sb;
			gfs2_sb_in(&sb, data);
			gfs2_sb_print(&sb);
		}
		break;

	case GFS2_METATYPE_RG:
		printf("Resource Group Header\n");
		if (verbose) {
			struct gfs2_rgrp rg;
			gfs2_rgrp_in(&rg, data);
			gfs2_rgrp_print(&rg);
			if (verbose > 1)
				print_bitmaps(data, sizeof(struct gfs2_rgrp));
		}
		break;

	case GFS2_METATYPE_RB:
		printf("Resource Group Bitmap\n");
		if (verbose) {
			gfs2_meta_header_print(&mh);
			if (verbose > 1)
				print_bitmaps(data, sizeof(struct gfs2_meta_header));
		}
		break;

	case GFS2_METATYPE_DI:
		printf("Dinode\n");
		if (verbose) {
			struct gfs2_dinode di;
			gfs2_dinode_in(&di, data);
			gfs2_dinode_print(&di);
			if (verbose > 1) {
				if (di.di_height)
					print_pointers(data, sizeof(struct gfs2_dinode));
				else {
					if (S_ISREG(di.di_mode))
						printf("\n  stuffed data\n");
					else if (S_ISDIR(di.di_mode) &&
						(di.di_flags & GFS2_DIF_EXHASH))
						print_stuffed_hash(data);
					else if (S_ISDIR(di.di_mode))
						print_dirents(data, sizeof(struct gfs2_dinode));
					else if (S_ISLNK(di.di_mode))
						printf("\nsymlink to %s\n",
						       data + sizeof(struct gfs2_dinode));
				}
			}
		}
		break;

	case GFS2_METATYPE_IN:
		printf("Indirect\n");
		if (verbose) {
			gfs2_meta_header_print(&mh);
			if (verbose > 1) {
				print_pointers(data, sizeof(struct gfs2_meta_header));
			}
		}
		break;

	case GFS2_METATYPE_LF:
		printf("Directory Leaf\n");
		if (verbose) {
			struct gfs2_leaf lf;
			gfs2_leaf_in(&lf, data);
			gfs2_leaf_print(&lf);
			if (verbose > 1)
				print_dirents(data, sizeof(struct gfs2_leaf));
		}
		break;

	case GFS2_METATYPE_JD:
		printf("Journaled Data\n");
		if (verbose)
			gfs2_meta_header_print(&mh);
		break;

	case GFS2_METATYPE_LH:
		printf("Log Header\n");
		if (verbose) {
			struct gfs2_log_header lh;
			gfs2_log_header_in(&lh, data);
			gfs2_log_header_print(&lh);
		}
		break;

	case GFS2_METATYPE_LD:
		printf("Lock Descriptor\n");
		if (verbose) {
			struct gfs2_log_descriptor ld;
			gfs2_log_descriptor_in(&ld, data);
			gfs2_log_descriptor_print(&ld);
		}
		break;

	case GFS2_METATYPE_LB:
		printf("Generic Log Block\n");
		if (verbose)
			gfs2_meta_header_print(&mh);
		break;

	case GFS2_METATYPE_EA:
		printf("Extended Attribute\n");
		if (verbose)
			gfs2_meta_header_print(&mh);
		break;

	case GFS2_METATYPE_ED:
		printf("Extended Attribute Data\n");
		if (verbose)
			gfs2_meta_header_print(&mh);
		break;

	case GFS2_METATYPE_UT:
		printf("Unlinked Tags\n");
		if (verbose)
			gfs2_meta_header_print(&mh);
		break;

	case GFS2_METATYPE_QC:
		printf("Quota Changes\n");
		if (verbose)
			gfs2_meta_header_print(&mh);
		break;

	default:
		printf("Unknown metadata type\n");
		if (verbose)
			gfs2_meta_header_print(&mh);
		break;
	}

	free(data);
}

