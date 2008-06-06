#ifndef __UTIL_H__
#define __UTIL_H__

#include "fsck_incore.h"

#define do_lseek(fd, off) \
  ((lseek((fd), (off), SEEK_SET) == (off)) ? 0 : -1)

#define do_read(fd, buff, len) \
  ((read((fd), (buff), (len)) == (len)) ? 0 : -1)

#define do_write(fd, buff, len) \
  ((write((fd), (buff), (len)) == (len)) ? 0 : -1)


int compute_height(struct fsck_sb *sdp, uint64 sz);
int check_range(struct fsck_sb *sdp, uint64 blkno);
int set_meta(osi_buf_t *bh, int type, int format);
int check_type(osi_buf_t *bh, int type);
int check_meta(osi_buf_t *bh, int type);
int next_rg_meta(struct fsck_rgrp *rgd, uint64 *block, int first);
int next_rg_meta_free(struct fsck_rgrp *rgd, uint64 *block, int first, int *free);
int next_rg_metatype(struct fsck_rgrp *rgd, uint64 *block, uint32 type, int first);
struct di_info *search_list(osi_list_t *list, uint64 addr);
void warm_fuzzy_stuff(uint64_t block);

#endif /* __UTIL_H__ */
