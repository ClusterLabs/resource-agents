#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <inttypes.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <limits.h>
#include <errno.h>

#define __user
#include "gfs_ioctl.h"
#include "gfs_ondisk.h"

#include "osi_list.h"
#include "linux_endian.h"

#include "gfs_tool.h"

#define LAYOUT_DATA_QUANTUM (4194304)

struct extent {
	osi_list_t list;

	uint64_t offset;
	uint64_t start;
	unsigned int len;
};
typedef struct extent extent_t;

struct buffer {
	osi_list_t list;
	uint64_t blkno;
	char *data;

	int touched;
};
typedef struct buffer buffer_t;

struct world {
	char *buf_data;
	unsigned int buf_size;
	int buf_count;
	osi_list_t blist;
	osi_list_t elist;

	struct gfs_sb sb;
	unsigned int diptrs;
	unsigned int inptrs;
	unsigned int jbsize;
	unsigned int hash_bsize;
	unsigned int hash_ptrs;

	buffer_t *dibh;
	struct gfs_dinode di;
};
typedef struct world world_t;

typedef void (*pointer_call_t) (world_t *w,
				unsigned int height, uint64_t bn, void *data);
typedef void (*leaf_call_t) (world_t *w,
			     uint32_t index, uint32_t len, uint64_t leaf_no,
			     void *data);

/**
 * build_list - build a list of buffer_t's to represent the data from the kernel
 * @w: the world structure
 *
 */

static void
build_list(world_t *w)
{
	buffer_t *b;
	unsigned int x;

	for (x = 0; x < w->buf_count; x += sizeof(uint64_t) + w->sb.sb_bsize) {
		b = malloc(sizeof(buffer_t));
		if (!b)
			die("out of memory\n");

		memset(b, 0, sizeof(buffer_t));

		b->blkno = *(uint64_t *) (w->buf_data + x);
		b->data = w->buf_data + x + sizeof(uint64_t);

		osi_list_add_prev(&b->list, &w->blist);
	}

	if (x != w->buf_count)
		die("the kernel passed back unaligned data\n");
}

/**
 * check_list - check the buffers passed back by the kernel
 * @w: the world
 *
 */

static void
check_list(world_t *w)
{
	osi_list_t *tmp;
	buffer_t *b;
	struct gfs_meta_header mh;
	char *type;

	for (tmp = w->blist.next; tmp != &w->blist; tmp = tmp->next) {
		b = osi_list_entry(tmp, buffer_t, list);

		gfs_meta_header_in(&mh, b->data);

		if (mh.mh_magic != GFS_MAGIC)
			die("bad magic number on block\n");

		switch (mh.mh_type) {
		case GFS_METATYPE_DI:
			type = "GFS_METATYPE_DI";

			if (w->dibh)
				die("more than one dinode in file\n");
			else {
				w->dibh = b;
				gfs_dinode_in(&w->di, b->data);

				b->touched = TRUE;
			}

			break;
		case GFS_METATYPE_IN:
			type = "GFS_METATYPE_IN";
			break;
		case GFS_METATYPE_LF:
			type = "GFS_METATYPE_LF";
			break;
		case GFS_METATYPE_JD:
			type = "GFS_METATYPE_JD";
			break;
		case GFS_METATYPE_EA:
			type = "GFS_METATYPE_EA";
			break;
		case GFS_METATYPE_ED:
			die("GFS_METATYPE_ED shouldn't be present\n");
		default:
			die("strange meta type\n");
		}
	}

	if (!w->dibh)
		die("no dinode\n");
}

/**
 * getbuf - get the buffer_t for a given block number
 * @w: the world
 * @blkno: the block number
 *
 * Returns: the buffer_t
 */

static buffer_t *
getbuf(world_t *w, uint64_t blkno)
{
	osi_list_t *tmp;
	buffer_t *b;

	for (tmp = w->blist.next; tmp != &w->blist; tmp = tmp->next) {
		b = osi_list_entry(tmp, buffer_t, list);
		if (b->blkno == blkno) {
			osi_list_del(&b->list);
			osi_list_add(&b->list, &w->blist);

			b->touched = TRUE;

			return b;
		}
	}

	die("buffer not found\n");
}

/**
 * recursive_scan - call a function for each block pointer in a file
 * @w: the world
 * @height: the height of the block being pointed to
 * @block: the block being pointed to
 * @pc: the function to call
 * @data: private data for the @pc function
 *
 */

static void
recursive_scan(world_t *w,
	       unsigned int height, uint64_t block, pointer_call_t pc, void *data)
{
	buffer_t *b = NULL;
	uint64_t *top, *bottom;
	uint64_t bn;

	if (!height) {
		b = w->dibh;

		top = (uint64_t *) (b->data + sizeof(struct gfs_dinode));
		bottom =
		    (uint64_t *) (b->data + sizeof(struct gfs_dinode)) +
		    w->diptrs;
	} else {
		b = getbuf(w, block);

		top = (uint64_t *) (b->data + sizeof(struct gfs_indirect));
		bottom =
		    (uint64_t *) (b->data + sizeof(struct gfs_indirect)) +
		    w->inptrs;
	}

	for (; top < bottom; top++) {
		bn = gfs64_to_cpu(*top);

		pc(w, height, bn, data);

		if (bn && height < w->di.di_height - 1)
			recursive_scan(w, height + 1, bn, pc, data);
	}
}

/**
 * bmap - return the buffer_t for a given logical block in the file
 * @w: the world
 * @lbn: the logical block number
 *
 * Returns: the buffer_t
 */

static buffer_t *
bmap(world_t *w, uint64_t lbn)
{
	osi_list_t *tmp;
	extent_t *e;

	for (tmp = w->elist.next; tmp != &w->elist; tmp = tmp->next) {
		e = osi_list_entry(tmp, extent_t, list);

		if (e->offset <= lbn && lbn < e->offset + e->len)
			return getbuf(w, e->start + lbn - e->offset);
	}

	return NULL;
}

/**
 * journaled_read - read some of the contents of a journaled file
 * @w: the world
 * @buf: the buffer to read into
 * @offset: the offset to read from
 * @size: the number of bytes to read
 *
 */

static void
journaled_read(world_t *w, char *buf, uint64_t offset, unsigned int size)
{
	buffer_t *b;
	uint64_t lbn;
	unsigned int o, chunk;

	if (!(w->di.di_flags & GFS_DIF_JDATA))
		die("not a journaled file\n");

	if (!w->di.di_height) {
		if (offset >= w->sb.sb_bsize - sizeof(struct gfs_dinode))
			memset(buf, 0, size);
		else {
			chunk =
			    w->sb.sb_bsize - sizeof(struct gfs_dinode) -
			    offset;
			if (chunk > size)
				chunk = size;
			memcpy(buf,
			       w->dibh->data + sizeof(struct gfs_dinode) +
			       offset, chunk);
			if (chunk < size)
				memset(buf + chunk, 0, size - chunk);
		}
	} else
		while (size) {
			lbn = offset / w->jbsize;
			o = offset % w->jbsize;
			chunk = (size > w->jbsize - o) ? (w->jbsize - o) : size;

			b = bmap(w, lbn);
			if (b)
				memcpy(buf,
				       b->data +
				       sizeof(struct gfs_meta_header) + o,
				       chunk);
			else
				memset(buf, 0, chunk);

			buf += chunk;
			offset += chunk;
			size -= chunk;
		}
}

/**
 * foreach_leaf - call a function for each leaf in a directory
 * @w: the world
 * @lc: the function to call for each each
 * @data: private data to pass to it
 *
 * Returns: 0 on success, -EXXX on failure
 */

static void
foreach_leaf(world_t *w, leaf_call_t lc, void *data)
{
	buffer_t *b;
	struct gfs_leaf leaf;
	uint32_t hsize, len;
	uint32_t ht_offset, lp_offset, ht_offset_cur = -1;
	uint32_t index = 0;
	uint64_t lp[w->hash_ptrs];
	uint64_t leaf_no;

	hsize = 1 << w->di.di_depth;
	if (hsize * sizeof(uint64_t) != w->di.di_size)
		die("bad hash table size\n");

	while (index < hsize) {
		lp_offset = index % w->hash_ptrs;
		ht_offset = index - lp_offset;

		if (ht_offset_cur != ht_offset) {
			journaled_read(w, (char *) lp,
				       ht_offset * sizeof(uint64_t),
				       w->hash_bsize);
			ht_offset_cur = ht_offset;
		}

		leaf_no = gfs64_to_cpu(lp[lp_offset]);
		if (!leaf_no)
			die("NULL leaf pointer\n");

		b = getbuf(w, leaf_no);
		gfs_leaf_in(&leaf, b->data);

		len = 1 << (w->di.di_depth - leaf.lf_depth);

		lc(w, index, len, leaf_no, data);

		index += len;
	}

	if (index != hsize)
		die("screwed up directory\n");
}

/**
 * add_extent - add an extend to the list of the file's data extents
 * @w: the world
 * @offset: the starting logical block of the extent
 * @start: the starting disk block of the extent
 * @len: the number of blocks in the extent
 *
 */

static void
add_extent(world_t *w, uint64_t offset, uint64_t start, unsigned int len)
{
	extent_t *e;

	e = malloc(sizeof(extent_t));
	if (!e)
		die("out of memory\n");

	memset(e, 0, sizeof(extent_t));

	e->offset = offset;
	e->start = start;
	e->len = len;

	osi_list_add_prev(&e->list, &w->elist);
}

struct do_pf_s {
	unsigned int height;
	uint64_t offset;
	uint64_t start;
	uint64_t skip;
	unsigned int len;
};
typedef struct do_pf_s do_pf_t;

/**
 * do_pf: called for every pointer in the file (prints/collects extent info)
 * @w: the world
 * @height: the height of the block containing the pointer
 * @bn: the contents of the pointer
 * @data: a do_pf_t structure
 *
 */

static void
do_pf(world_t *w, unsigned int height, uint64_t bn, void *data)
{
	do_pf_t *pf = (do_pf_t *) data;
	unsigned int x;
	uint64_t skip;

	if (pf->height < height + 1)
		return;

	if (!bn) {
		if (pf->height == height + 1)
			pf->skip++;
		else {
			x = pf->height - height - 1;
			skip = w->inptrs;
			while (--x)
				skip *= w->inptrs;
			pf->skip += skip;
		}

		return;
	}

	if (pf->height == height + 1) {
		if (pf->start + pf->len == bn && pf->len == pf->skip) {
			pf->len++;
			pf->skip++;
		} else {
			if (pf->start) {
				printf("  %-20" PRIu64 " %-20" PRIu64 " %-20"
				       PRIu64 " %u\n", pf->offset,
				       pf->offset + pf->len - 1, pf->start,
				       pf->len);
				if (pf->height == w->di.di_height)
					add_extent(w, pf->offset, pf->start,
						   pf->len);
			}

			pf->offset += pf->skip;
			pf->start = bn;
			pf->len = 1;
			pf->skip = 1;
		}
	}
}

/**
 * print_file - print out the extent lists for all the heights of a file
 * @w: the world
 *
 */

static void
print_file(world_t *w)
{
	do_pf_t pf;
	unsigned int h;
	char *type;

	switch (w->di.di_type) {
	case GFS_FILE_REG:
		type = "File";
		break;
	case GFS_FILE_DIR:
		type = "Directory";
		break;
	case GFS_FILE_LNK:
		type = "Symbolic Link";
		break;
	case GFS_FILE_BLK:
		type = "Block Device";
		break;
	case GFS_FILE_CHR:
		type = "Character Device";
		break;
	case GFS_FILE_FIFO:
		type = "FIFO";
		break;
	case GFS_FILE_SOCK:
		type = "Socket";
		break;
	default:
		die("strange file type\n");
	};

	printf("%s dinode:\n", type);
	printf("  %" PRIu64 "\n", w->di.di_num.no_addr);

	if (!w->di.di_height) {
		if (w->di.di_type == GFS_FILE_DIR) {
			if (w->di.di_flags & GFS_DIF_EXHASH)
				printf("\nStuffed hash table\n");
		} else
			printf("\nStuffed file data\n");

		return;
	}

	for (h = 1; h <= w->di.di_height; h++) {
		if (w->di.di_type == GFS_FILE_DIR)
			type =
			    (h == w->di.di_height) ? "hash table" : "indirect";
		else
			type = (h == w->di.di_height) ? "data" : "indirect";

		printf("\n");
		printf("At height %u (%s):\n", h, type);
		printf("  %-20s %-20s %-20s %s\n",
		       "From LBlock", "To LBlock", "DBlock", "Blocks");

		memset(&pf, 0, sizeof(do_pf_t));
		pf.height = h;

		recursive_scan(w, 0, 0, do_pf, &pf);

		if (pf.start) {
			printf("  %-20" PRIu64 " %-20" PRIu64 " %-20" PRIu64
			       " %u\n", pf.offset, pf.offset + pf.len - 1,
			       pf.start, pf.len);
			if (h == w->di.di_height)
				add_extent(w, pf.offset, pf.start, pf.len);
		}
	}
}

/**
 * do_lc - print out info about a leaf block
 * @w: the world
 * @index: the index of the leaf
 * @len: the number of pointers to the leaf
 * @leaf_no: the leaf block number
 * @data: unused
 *
 */

static void
do_lc(world_t *w, uint32_t index, uint32_t len, uint64_t leaf_no, void *data)
{
	buffer_t *b;
	struct gfs_leaf leaf;
	uint64_t blk;

	for (blk = leaf_no; blk; blk = leaf.lf_next) {
		b = getbuf(w, blk);
		gfs_leaf_in(&leaf, b->data);

		printf("  %.8X             %.8X             %-20" PRIu64
		       " %u\n", index << (32 - w->di.di_depth),
		       ((index + len) << (32 - w->di.di_depth)) - 1, blk,
		       leaf.lf_entries);
	}

}

/**
 * print_leaves - print out the location of the exhash leaves
 * @w: the world
 *
 */

static void
print_leaves(world_t *w)
{
	printf("\n");

	if (w->di.di_flags & GFS_DIF_EXHASH) {
		printf("Leaves:\n");
		printf("  %-20s %-20s %-20s %s\n",
		       "From Hash", "To Hash", "DBlock", "Entries");
		foreach_leaf(w, do_lc, NULL);
	} else
		printf("Stuffed directory data\n");
}

/**
 * print_eattr_data - print out the locations of the eattr data blocks
 * @w: the world
 *
 */

#define MAKE_MULT8(x) (((x) + 7) & ~7)
#define GFS_EA_REC_LEN(ea) gfs32_to_cpu((ea)->ea_rec_len)
#define GFS_EA_IS_STUFFED(ea) (!(ea)->ea_num_ptrs)
#define GFS_EA_IS_LAST(ea) ((ea)->ea_flags & GFS_EAFLAG_LAST)
#define GFS_EA2NAME(ea) ((char *)((struct gfs_ea_header *)(ea) + 1))
#define GFS_EA2DATAPTRS(ea) \
((uint64_t *)(GFS_EA2NAME(ea) + MAKE_MULT8((ea)->ea_name_len)))
#define GFS_EA2NEXT(ea) \
((struct gfs_ea_header *)((char *)(ea) + GFS_EA_REC_LEN(ea)))
#define GFS_EA_BH2FIRST(b) \
((struct gfs_ea_header *)((b)->data + \
			  sizeof(struct gfs_meta_header)))

static void
print_eattr_data(world_t *w, uint64_t blkno, int *first)
{
	buffer_t *b = getbuf(w, blkno);
	struct gfs_ea_header *ea;

	ea = GFS_EA_BH2FIRST(b);
	for (;;) {
		if (!GFS_EA_IS_STUFFED(ea)) {
			char name[300];
			uint64_t *p, blkno;
			uint64_t b;
			unsigned int l;
			unsigned int x;
			int c;

			if (*first) {
				printf("\nExtended Attributes data blocks:\n");
				printf("  %-20s %-10s %s\n",
				       "DBlock", "Blocks", "Name");
				*first = FALSE;
			}

			if (ea->ea_type == GFS_EATYPE_UNUSED)
				strcpy(name, "unused");
			else {
				unsigned int x;
				switch (ea->ea_type) {
				case GFS_EATYPE_USR:
					strcpy(name, "user.");
					break;
				case GFS_EATYPE_SYS:
					strcpy(name, "system.");
					break;
				default:
					strcpy(name, "unknown.");
					break;
				}
				x = strlen(name);
				memcpy(name + x,
				       GFS_EA2NAME(ea), ea->ea_name_len);
				name[x + ea->ea_name_len] = 0;
			}

			b = 0;
			l = 0;
			c = FALSE;

			p = GFS_EA2DATAPTRS(ea);
			for (x = 0; x < ea->ea_num_ptrs; x++) {
				blkno = gfs64_to_cpu(*p);
				if (b + l == blkno)
					l++;
				else {
					if (b) {
						printf("  %-20" PRIu64
						       " %-10u %s\n", b, l,
						       name);
						if (!c) {
							strcat(name, " (cont)");
							c = TRUE;
						}
					}
					b = blkno;
					l = 1;
				}
				p++;
			}
			printf("  %-20" PRIu64 " %-10u %s\n", b, l, name);
		}
		if (GFS_EA_IS_LAST(ea))
			break;
		ea = GFS_EA2NEXT(ea);
	}
}

/**
 * print_eattr - print out the locations of the eattr blocks
 * @w: the world
 *
 */

static void
print_eattr(world_t *w)
{
	int first = TRUE;

	if (w->di.di_flags & GFS_DIF_EA_INDIRECT) {
		buffer_t *b = getbuf(w, w->di.di_eattr);
		uint64_t *blkno;
		unsigned int x;

		printf("\nExtended Attribute indirect block:\n");
		printf("  %" PRIu64 "\n", w->di.di_eattr);

		printf("\nExtended Attribute blocks:\n");
		blkno = (uint64_t *) (b->data + sizeof(struct gfs_indirect));
		for (x = 0; x < w->inptrs; x++) {
			if (!*blkno)
				break;
			printf("  %" PRIu64 "\n", gfs64_to_cpu(*blkno));
			blkno++;
		}

		blkno = (uint64_t *) (b->data + sizeof(struct gfs_indirect));
		for (x = 0; x < w->inptrs; x++) {
			if (!*blkno)
				break;
			print_eattr_data(w, gfs64_to_cpu(*blkno), &first);
			blkno++;
		}
	} else {
		printf("\nExtended Attribute block:\n");
		printf("  %" PRIu64 "\n", w->di.di_eattr);

		print_eattr_data(w, w->di.di_eattr, &first);
	}
}

/**
 * check_for_untouched_buffers - 
 * @w: the world
 *
 */

static void
check_for_untouched_buffers(world_t *w)
{
	osi_list_t *tmp;
	buffer_t *b;

	for (tmp = w->blist.next; tmp != &w->blist; tmp = tmp->next) {
		b = osi_list_entry(tmp, buffer_t, list);
		if (b->touched)
			continue;

		printf("Buffer %" PRIu64 " untouched\n", b->blkno);
	}
}

/**
 * print_layout - print out the ondisk layout of a file
 * @argc:
 * @argv:
 *
 */

void
print_layout(int argc, char **argv)
{
	world_t w;
	char *path;
	int fd;
	int retry = TRUE;
	struct gfs_ioctl gi;
	int error;

	memset(&w, 0, sizeof(world_t));
	w.buf_size = LAYOUT_DATA_QUANTUM;
	osi_list_init(&w.blist);
	osi_list_init(&w.elist);

	if (optind == argc)
		die("Usage: gfs_tool layout <filename> [buffersize]\n");

	path = argv[optind++];
	if (optind < argc ) {
		w.buf_size = atoi(argv[3]);
		retry = FALSE;
	}

	fd = open(path, O_RDONLY);
	if (fd < 0)
		die("can't open %s: %s\n", path, strerror(errno));

	check_for_gfs(fd, path);

	{
		char *argv[] = { "get_super" };

		gi.gi_argc = 1;
		gi.gi_argv = argv;
		gi.gi_data = (char *)&w.sb;
		gi.gi_size = sizeof(struct gfs_sb);

		error = ioctl(fd, GFS_IOCTL_SUPER, &gi);
		if (error != gi.gi_size)
			die("error doing get_super (%d): %s\n",
			    error, strerror(errno));
	}

	w.diptrs = (w.sb.sb_bsize - sizeof(struct gfs_dinode)) /
		sizeof(uint64_t);
	w.inptrs = (w.sb.sb_bsize - sizeof(struct gfs_indirect)) /
		sizeof(uint64_t);
	w.jbsize = w.sb.sb_bsize - sizeof(struct gfs_meta_header);
	w.hash_bsize = w.sb.sb_bsize / 2;
	w.hash_ptrs = w.hash_bsize / sizeof(uint64_t);

	for (;;) {
		char *argv[] = { "get_file_meta" };

		w.buf_data = malloc(w.buf_size);
		if (!w.buf_data)
			die("out of memory\n");

		gi.gi_argc = 1;
		gi.gi_argv = argv;
		gi.gi_data = w.buf_data;
		gi.gi_size = w.buf_size;

		w.buf_count = ioctl(fd, GFS_IOCTL_SUPER, &gi);
		if (w.buf_count >= 0)
			break;

		if (errno == ENOMEM) {
			if (retry) {
				free(w.buf_data);
				w.buf_size += LAYOUT_DATA_QUANTUM;
				continue;
			} else
				die("%u bytes isn't enough memory\n",
				    w.buf_size);
		}
		die("error doing get_file_meta: %s\n",
		    strerror(errno));
	}

	build_list(&w);
	check_list(&w);

	print_file(&w);

	if (w.di.di_type == GFS_FILE_DIR)
		print_leaves(&w);

	if (w.di.di_eattr)
		print_eattr(&w);

	check_for_untouched_buffers(&w);

	close(fd);
}
