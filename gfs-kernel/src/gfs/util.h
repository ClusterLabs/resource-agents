/******************************************************************************
*******************************************************************************
**
**  Copyright (C) Sistina Software, Inc.  1997-2003  All rights reserved.
**  Copyright (C) 2004 Red Hat, Inc.  All rights reserved.
**
**  This copyrighted material is made available to anyone wishing to use,
**  modify, copy, or redistribute it subject to the terms and conditions
**  of the GNU General Public License v.2.
**
*******************************************************************************
******************************************************************************/

#ifndef __UTIL_DOT_H__
#define __UTIL_DOT_H__


/* Utility functions */

extern uint32_t gfs_random_number;
uint32_t gfs_random(void);

uint32_t gfs_hash(const void *data, unsigned int len);
uint32_t gfs_hash_more(const void *data, unsigned int len, uint32_t hash);

void gfs_sort(void *array, unsigned int num, unsigned int size,
	      int (*compare) (void *, void *));

void bitch_about(struct gfs_sbd *sdp, unsigned long *last, char *about);



/* Assertion stuff */

#define GFS_ASSERT_TYPE_NONE      (18)
#define GFS_ASSERT_TYPE_SBD       (19)
#define GFS_ASSERT_TYPE_GLOCK     (20)
#define GFS_ASSERT_TYPE_INODE     (21)
#define GFS_ASSERT_TYPE_RGRPD     (22)

#define GFS_ASSERT(x, todo) \
do \
{ \
  if (!(x)) \
  { \
    {todo} \
    gfs_assert_i(#x, GFS_ASSERT_TYPE_NONE, NULL, __FILE__, __LINE__); \
 } \
} \
while (0)

#define GFS_ASSERT_SBD(x, sdp, todo) \
do \
{ \
  if (!(x)) \
  { \
    struct gfs_sbd *gfs_assert_sbd = (sdp); \
    {todo} \
    gfs_assert_i(#x, GFS_ASSERT_TYPE_SBD, gfs_assert_sbd, __FILE__, __LINE__); \
  } \
} \
while (0)

#define GFS_ASSERT_GLOCK(x, gl, todo) \
do \
{ \
  if (!(x)) \
  { \
    struct gfs_glock *gfs_assert_glock = (gl); \
    {todo} \
    gfs_assert_i(#x, GFS_ASSERT_TYPE_GLOCK, gfs_assert_glock, __FILE__, __LINE__); \
  } \
} \
while (0)

#define GFS_ASSERT_INODE(x, ip, todo) \
do \
{ \
  if (!(x)) \
  { \
    struct gfs_inode *gfs_assert_inode = (ip); \
    {todo} \
    gfs_assert_i(#x, GFS_ASSERT_TYPE_INODE, gfs_assert_inode, __FILE__, __LINE__); \
  } \
} \
while (0)

#define GFS_ASSERT_RGRPD(x, rgd, todo) \
do \
{ \
  if (!(x)) \
  { \
    struct gfs_rgrpd *gfs_assert_rgrpd = (rgd); \
    {todo} \
    gfs_assert_i(#x, GFS_ASSERT_TYPE_RGRPD, gfs_assert_rgrpd, __FILE__, __LINE__); \
  } \
} \
while (0)

extern volatile int gfs_in_panic;
void gfs_assert_i(char *assertion,
		  unsigned int type, void *ptr,
		  char *file, unsigned int line) __attribute__ ((noreturn));


/* I/O error stuff */

#define GFS_IO_ERROR_TYPE_NONE    (118)
#define GFS_IO_ERROR_TYPE_BH      (119)
#define GFS_IO_ERROR_TYPE_INODE   (120)

#define gfs_io_error(sdp) \
gfs_io_error_i((sdp), GFS_ASSERT_TYPE_NONE, NULL, __FILE__, __LINE__);

#define gfs_io_error_bh(sdp, bh) \
do \
{ \
  struct buffer_head *gfs_io_error_bh = (bh); \
  gfs_io_error_i((sdp), GFS_IO_ERROR_TYPE_BH, gfs_io_error_bh, __FILE__, __LINE__); \
} \
while (0)

#define gfs_io_error_inode(ip) \
do \
{ \
  struct gfs_inode *gfs_io_error_inode = (ip); \
  gfs_io_error_i((ip)->i_sbd, GFS_IO_ERROR_TYPE_INODE, gfs_io_error_inode, __FILE__, __LINE__); \
} \
while (0)

void gfs_io_error_i(struct gfs_sbd *sdp,
		    unsigned int type, void *ptr,
		    char *file, unsigned int line);


/* Memory stuff */

#define RETRY_MALLOC(do_this, until_this) \
for (;;) \
{ \
  do { do_this; } while (0); \
  if (until_this) \
    break; \
  printk("GFS: out of memory: %s, %u\n", __FILE__, __LINE__); \
  yield();\
}

extern kmem_cache_t *gfs_glock_cachep;
extern kmem_cache_t *gfs_inode_cachep;
extern kmem_cache_t *gfs_bufdata_cachep;
extern kmem_cache_t *gfs_mhc_cachep;

void *gmalloc(unsigned int size);


#endif /* __UTIL_DOT_H__ */
