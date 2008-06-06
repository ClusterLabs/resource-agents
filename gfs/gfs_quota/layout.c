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

#include "gfs_quota.h"

#define LAYOUT_DATA_QUANTUM (4194304)

extern void
print_quota(commandline_t *comline,
            int user, uint32_t id,
            struct gfs_quota *q,
            struct gfs_sb *sb);

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

struct lblk_range {
        osi_list_t list;
        
        uint64_t start;
        unsigned int len;
};
typedef struct lblk_range lblk_range_t;

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

static lblk_range_t *lblk_range_list;
static unsigned int j_blk_size;

/**
 * add_lblk_range - Add a range of logical block numbers to lblk_list
 * @lblk_list: the list to add to
 * @start: the starting block number of the range
 * @len: the length of the range
 *
 */

static void 
add_lblk_range(lblk_range_t *lblk_list, uint64_t start, unsigned int len)
{
        lblk_range_t *tmp;

        tmp = malloc(sizeof(lblk_range_t));
        if (!tmp)
                die("out of memory\n");
        
        tmp->start = start;
        tmp->len = len;
        
        osi_list_add_prev(&tmp->list, &lblk_list->list);
}

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
recursive_scan(world_t *w, unsigned int height, 
               uint64_t block, pointer_call_t pc, void *data)
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
                                if (pf->height == w->di.di_height) {
                                        add_extent(w, pf->offset, pf->start,
                                                   pf->len);
                                        add_lblk_range(lblk_range_list, 
                                                       pf->offset, pf->len);
                                }
                        }

                        pf->offset += pf->skip;
                        pf->start = bn;
                        pf->len = 1;
                        pf->skip = 1;
                }
        }
}

/**
 * get_dblk_ranges - Run through the file and add valid data block ranges to 
 *                   lblk_range_list
 * @w: the world
 *
 */

static void
get_dblk_ranges(world_t *w)
{
        do_pf_t pf;
        unsigned int h;

        for (h = 1; h <= w->di.di_height; h++) {

                memset(&pf, 0, sizeof(do_pf_t));
                pf.height = h;

                recursive_scan(w, 0, 0, do_pf, &pf);

                if (pf.start) {
                        if (h == w->di.di_height) {
                                add_extent(w, pf.offset, pf.start, pf.len);
                                add_lblk_range(lblk_range_list, pf.offset, 
                                               pf.len);
                        }
                }
        }
}

/**
 * compute_layout - Computes the layout and store the valid data page ranges 
 *                  in lblk_range_list
 * @argc:
 * @argv:
 *
 */

void
compute_layout(char *path)
{
        world_t w;
        int fd;
        int retry = TRUE;
        struct gfs_ioctl gi;
        int error;

        memset(&w, 0, sizeof(world_t));
        w.buf_size = LAYOUT_DATA_QUANTUM;
        osi_list_init(&w.blist);
        osi_list_init(&w.elist);


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
        j_blk_size = w.jbsize;
        w.hash_bsize = w.sb.sb_bsize / 2;
        w.hash_ptrs = w.hash_bsize / sizeof(uint64_t);

        for (;;) {
                char *argv[] = { "get_file_meta_quota" };

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

        get_dblk_ranges(&w);

        close(fd);
}

/**
 * print_quota_range - print the quota information in the given sequence of 
 *                     blocks
 * @argc:
 * @argv:
 *
 */
void 
print_quota_range(commandline_t *comline, int type, uint64_t start, 
                  unsigned int len)
{
        uint64_t blk_offt, blk_end_offt, offset;
        uint64_t quo;
        unsigned int rem;
        int fd;
        struct gfs_sb sb;
        struct gfs_ioctl gi;
        char buf[sizeof(struct gfs_quota)];
        struct gfs_quota q;
        uint64_t hidden_blocks = 0;
        uint32_t id;
        int error;

        blk_offt = start * (uint64_t)j_blk_size;
        blk_end_offt = blk_offt + (uint64_t)(len * j_blk_size);

        quo = blk_offt / (uint64_t)(2 * sizeof(struct gfs_quota));
        rem = blk_offt % (uint64_t)(2 * sizeof(struct gfs_quota));
        
        offset = type == GQ_ID_USER ? blk_offt : 
                blk_offt + sizeof(struct gfs_quota);
        
        if (rem && type == GQ_ID_USER)
                offset = (quo + 1) * (uint64_t)(2 * sizeof(struct gfs_quota));
        
        if (rem && type == GQ_ID_GROUP) {
                if (rem <= sizeof(struct gfs_quota))
                        offset = quo * (uint64_t)(2 * sizeof(struct gfs_quota))
                                + sizeof(struct gfs_quota);
                else
                        offset = (quo + 1) * 
                                (uint64_t)(2 * sizeof(struct gfs_quota)) + 
                                sizeof(struct gfs_quota);
        }

        fd = open(comline->filesystem, O_RDONLY);
        if (fd < 0)
                die("can't open file %s: %s\n", comline->filesystem, 
                    strerror(errno));

        check_for_gfs(fd, comline->filesystem);
        do_get_super(fd, &sb);
        
        if (comline->no_hidden_file_blocks)
                hidden_blocks = compute_hidden_blocks(comline, fd);

        do {
                char *argv[] = { "do_hfile_read", "quota" };

                gi.gi_argc = 2;
                gi.gi_argv = argv;
                gi.gi_data = buf;
                gi.gi_size = sizeof(struct gfs_quota);
                gi.gi_offset = offset;
                
                memset(buf, 0, sizeof(struct gfs_quota));
                
                error = ioctl(fd, GFS_IOCTL_SUPER, &gi);
                if (error < 0)
                        die("can't read quota file: %s\n",
                            strerror(errno));
                
                gfs_quota_in(&q, buf);
                
                id = (offset / sizeof(struct gfs_quota)) >> 1;
                if (!id && comline->no_hidden_file_blocks)
                        q.qu_value -= hidden_blocks;
                
                if (q.qu_limit || q.qu_warn || q.qu_value)
                        print_quota(comline, type == GQ_ID_GROUP ? FALSE : TRUE,
                                    id, &q, &sb);

                offset += 2 * sizeof(struct gfs_quota);
        } while ((error == sizeof(struct gfs_quota)) && 
                 (offset < blk_end_offt));
        
        close(fd);
}

void 
print_quota_file(commandline_t *comline)
{
        lblk_range_t lblk_range, *bar;
        osi_list_t *t, *x;

        lblk_range_list = &lblk_range;

        osi_list_init(&lblk_range_list->list);
        
        compute_layout(comline->filesystem);
        
        if (osi_list_empty(&lblk_range_list->list)) {
                /* stuffed quota inode */
                print_quota_range(comline, GQ_ID_USER, 0, 1);
                print_quota_range(comline, GQ_ID_GROUP, 0, 1);
        } else {
                /* print the user quotas */
                osi_list_foreach(t, &lblk_range_list->list) {
                        bar = osi_list_entry(t, lblk_range_t, list);
                        print_quota_range(comline, GQ_ID_USER, bar->start, 
                                          bar->len);
                }
                /* print the group quotas */
                osi_list_foreach(t, &lblk_range_list->list) {
                        bar = osi_list_entry(t, lblk_range_t, list);
                        print_quota_range(comline, GQ_ID_GROUP, bar->start, 
                                          bar->len);
                }
                /* free the list */
                osi_list_foreach_safe(t, &lblk_range_list->list, x) {
                        bar = osi_list_entry(t, lblk_range_t, list);
                        osi_list_del(&bar->list);
                        free(bar);
                }
        }
}
