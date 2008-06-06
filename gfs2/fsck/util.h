#ifndef __UTIL_H__
#define __UTIL_H__

#include "libgfs2.h"

#define fsck_lseek(fd, off) \
  ((lseek((fd), (off), SEEK_SET) == (off)) ? 0 : -1)

int compute_height(struct gfs2_sbd *sdp, uint64_t sz);
struct di_info *search_list(osi_list_t *list, uint64_t addr);
void warm_fuzzy_stuff(uint64_t block);
const char *block_type_string(struct gfs2_block_query *q);

#endif /* __UTIL_H__ */
