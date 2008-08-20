/**
 * main.c - Functions for parsing the on-disk structure of a GFS2 fs.
 *          Written by Andrew Price
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <string.h>
#include <fcntl.h>
#include <libgfs2.h>
#include <errno.h>

void (*report_func)(long int blk, char *type, long int parent, char *fn);

/* Define how the block information is reported */
#define report(b,p,t,n) \
	((*report_func)((long int)b, t, (long int)p,n))
/*	(printf("%lu\t%s\t%lu\t%s\n", (long int)b, t, (long int)p, f)) */
#define report_data(b,p) report(b,p,"D","")
#define report_leaf(b,p) report(b,p,"L","")
#define report_indir(b,p) report(b,p,"i","")
#define report_inode(b,p,n) report(b,p,"I",n)

struct blk_extended {
	uint64_t parent_blk;
	char *fname;
	char *blk;
};

/* FIXME: A static length block stack is most likely a stupid idea */
struct blkstack {
	int top;
	struct blk_extended blocks[1024];
};

struct blkstack blk_stack;
struct gfs2_sb sb;
uint32_t blk_size;
off_t max_seek;
int fd;
int flag_stop;

/* prog_name and print_it() are needed to satisfy externs in libgfs2 */
char *prog_name = "askant";

void print_it(const char *label, const char *fmt, const char *fmt2, ...)
{
	va_list args;

	va_start(args, fmt2);
	printf("%s = ", label);
	vprintf(fmt, args);
	printf("\n");
	va_end(args);
}

/**
 * Push a block onto the stack
 */
static void push_blk(char *blk, uint64_t parent, char *fn)
{
	struct blk_extended eblk;

	eblk.blk = blk;
	eblk.parent_blk = parent;
	eblk.fname = fn;

	blk_stack.top++;
	blk_stack.blocks[blk_stack.top] = eblk;
}

/**
 * Initialise the block stack
 */
static int blk_stack_init(void)
{
	blk_stack.top = -1;
	return 1;
}

/**
 * Pop a block from the stack
 */
static struct blk_extended pop_blk(void)
{
	return blk_stack.blocks[blk_stack.top--];
}

/**
 * Read the GFS2 superblock into the global sb variable.
 * Returns 1 on error, 0 on success.
 */
static int read_gfs2_sb(void)
{
	off_t offsetsb;
	off_t offsetres;
	unsigned char buffer[GFS2_BASIC_BLOCK];
	ssize_t readsz;

	offsetsb = GFS2_SB_ADDR * GFS2_BASIC_BLOCK;
	offsetres = lseek(fd, offsetsb, SEEK_SET);

	if (offsetres != offsetsb) {
		fprintf(stderr, "Could not seek to sb location on device.\n");
		return 0;
	}

	readsz = read(fd, buffer, GFS2_BASIC_BLOCK);
	if (readsz != GFS2_BASIC_BLOCK) {
		fprintf(stderr, "Could not read superblock.\n");
		return 0;
	}

	gfs2_sb_in(&sb, (char *)buffer);
	if (check_sb(&sb)) {
		fprintf(stderr, "Not a GFS2 filesystem.\n");
		return 0;
	}

	return 1;
}

/**
 * Read a block.
 * blk_offset must be a block number.
 * The returned pointer must be free'd.
 */
static char *read_gfs2_blk(off_t blk_offset)
{
	off_t offset;
	off_t offsetres;
	ssize_t readsz;
	char *buffer;

	buffer = (char *)malloc(blk_size);
	if (!buffer) {
		fprintf(stderr, "Could not allocate memory for block.\n");
		return NULL;
	}

	offset = blk_offset * blk_size;

	offsetres = lseek(fd, offset, SEEK_SET);
	if (offsetres != offset) {
		fprintf(stderr,
			"Could not seek to block location: %lu error: %s\n",
			(long int)blk_offset, strerror(errno));
		return NULL;
	}

	readsz = read(fd, buffer, blk_size);
	if (readsz != blk_size) {
		fprintf(stderr, "Could not read block: %lu\n",
					(long int)blk_offset);
		return NULL;
	}

	return buffer;
}

/**
 * Look at indirect pointers from a starting point in a block.
 */
static void do_indirect(char *start, uint16_t height, uint64_t parent)
{
	uint64_t ptr;
	unsigned int i;
	char *blk;

	for (i = 0; i < blk_size; i += sizeof(uint64_t)) {
		ptr = be64_to_cpu(*(uint64_t *)(start + i));
		if (ptr > 0 && ptr < (max_seek / blk_size)) {
			if (height == 1) {
				report_data(ptr, parent);
			} else if (height > 1) {
				blk = read_gfs2_blk(ptr);
				if (blk) {
					report_indir(ptr, parent);
					do_indirect(blk, height - 1, ptr);
					free(blk);
				}
			}
		} else {
			break;
		}
	}
}

/**
 * Parse count number of dirents from a starting point in a block.
 */
static void do_dirents(char *dirents, char *end, uint64_t parent, uint64_t gparent)
{
	struct gfs2_dirent dirent;
	char *di_blk;
	char *fname;

	while (dirents < end) {
		gfs2_dirent_in(&dirent, dirents);
		if (dirent.de_inum.no_addr &&
			dirent.de_inum.no_addr != parent &&
			dirent.de_inum.no_addr != gparent &&
			dirent.de_name_len > 0 &&
			dirent.de_name_len <= GFS2_FNAMESIZE) {

			fname = (char *)malloc(dirent.de_name_len + 1);
			if (!fname) {
				break;
			}

			memcpy(fname, dirents + sizeof(struct gfs2_dirent),
				dirent.de_name_len);
			fname[dirent.de_name_len] = '\0';

			di_blk = read_gfs2_blk(dirent.de_inum.no_addr);
			if (di_blk) {
				push_blk(di_blk, parent, fname);
			}
		}
		dirents += dirent.de_rec_len;
	}
}

/**
 * Examine the dirents in a leaf block.
 * If the leaf is chained, do the chained leaves too.
 */
static void do_leaf(char *blk, uint64_t parent, uint64_t gparent)
{
	struct gfs2_leaf leaf;

	while (blk) {
		gfs2_leaf_in(&leaf, blk);
		do_dirents(blk + sizeof(struct gfs2_leaf), blk + blk_size,
							parent, gparent);
		free(blk);
		if (!leaf.lf_next) {
			break;
		}

		blk = read_gfs2_blk(leaf.lf_next);
	}
}

/**
 * Parse leaf pointer data and examine the
 * dirents in the destination leaf blocks.
 */
static void do_leaves(char *start, uint64_t parent, uint64_t gparent)
{
	uint64_t ptr;
	uint64_t prev;
	unsigned int i;
	char *blk;

	prev = 0;
	for (i = 0; i < blk_size - sizeof(struct gfs2_dinode);
					i += sizeof(uint64_t)) {
		ptr = be64_to_cpu(*(uint64_t *)(start + i));

		if (ptr >= (max_seek / blk_size)) {
			break;
		}

		if (ptr && ptr != prev) {
			blk = read_gfs2_blk(ptr);
			if (blk) {
				report_leaf(ptr, parent);
				do_leaf(blk, parent, gparent);
			}
			prev = ptr;
		}
	}
}

/**
 * Parse inode data from a block
 */
static void do_inode_blk(char *blk, uint64_t parent, char *fname)
{
	struct gfs2_dinode di;
	char *data;

	gfs2_dinode_in(&di, blk);
	report_inode(di.di_num.no_addr, parent, fname);

	data = (char *)((struct gfs2_dinode *)blk + 1);

	if (di.di_height > 0) {
		/* Indirect pointers */
		do_indirect(data, di.di_height, di.di_num.no_addr);
	} else if (S_ISDIR(di.di_mode) && !(di.di_flags & GFS2_DIF_EXHASH)) {
		/* Stuffed directory */
		do_dirents(data, blk + blk_size, di.di_num.no_addr, parent);
	} else if (S_ISDIR(di.di_mode) && 
				(di.di_flags & GFS2_DIF_EXHASH) && 
				!(di.di_height)) {
		/* Directory, has hashtable, height == 0 */
		do_leaves(data, di.di_num.no_addr, parent);
	}

	/* free previously stacked block */
	free(fname);
	free(blk);
}

/**
 * Get the root dir block and parse the fs
 * using a stack to keep track of the unvisited
 * inode blocks.
 */
static void parse_fs(void)
{
	struct gfs2_inum *root_dir_inum;
	struct gfs2_inum *master_dir_inum;
	struct blk_extended blk;
	char *root_blk;
	char *master_blk;

	flag_stop = 0;

	root_dir_inum = &(sb.sb_root_dir);
	master_dir_inum = &(sb.sb_master_dir);

	root_blk = read_gfs2_blk(root_dir_inum->no_addr);
	master_blk = read_gfs2_blk(master_dir_inum->no_addr);
	if (!root_blk || !master_blk) {
		return;
	}

	push_blk(root_blk, root_dir_inum->no_addr, NULL);
	while (blk_stack.top >= 0 && !flag_stop) {
		blk = pop_blk();
		do_inode_blk(blk.blk, blk.parent_blk, blk.fname);
	}

	push_blk(master_blk, master_dir_inum->no_addr, NULL);
	while (blk_stack.top >= 0 && !flag_stop) {
		blk = pop_blk();
		/* TODO: Examine each block's magic number instead of assuming
		 * they're inodes. Omitted for now due to time constraints and
		 * the number of GFS2_METATYPE_*s which need catering for.
		 */
		do_inode_blk(blk.blk, blk.parent_blk, blk.fname);
	}
}

/**
 * Raise a flag to stop the parse loop cleanly
 */
void gfs2_stop(void)
{
	flag_stop = 1;
}

/**
 * Parse a gfs2 file system on a given device
 */
int gfs2_parse(char *dev, void (*func)(long int b, char *t, long int p, char *f))
{
	report_func = func;

	if (!blk_stack_init()) {
		return 0;
	}

	if ((fd = open(dev, O_RDONLY)) < 0) {
		return 0;
	}

	if (!read_gfs2_sb()) {
		close(fd);
		return 0;
	}

	blk_size = sb.sb_bsize;
	max_seek = lseek(fd, 0, SEEK_END);

	parse_fs();

	close(fd);

	return 1;
}

/**
 * Return the block size of the gfs2 file system on a given device.
 */
uint32_t gfs2_block_size(char *dev)
{
	if ((fd = open(dev, O_RDONLY)) < 0) {
		return 0;
	}

	if (!read_gfs2_sb()) {
		close(fd);
		return 0;
	}
	close(fd);
	return sb.sb_bsize;
}
