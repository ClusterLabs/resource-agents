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

void gfs_sort(void *base, unsigned int num_elem, unsigned int size,
	      int (*compar) (const void *, const void *));


/* Error handling */

#define gfs_assert(sdp, assertion, todo) \
do { \
	if (!(assertion)) { \
		{todo} \
		gfs_assert_i((sdp), #assertion, __FUNCTION__, __FILE__, __LINE__); \
	} \
} while (0)
void gfs_assert_i(struct gfs_sbd *sdp,
		  char *assertion,
		  const char *function,
		  char *file, unsigned int line)
__attribute__ ((noreturn));

void gfs_warn(struct gfs_sbd *sdp, char *fmt, ...)
__attribute__ ((format(printf, 2, 3)));

#define gfs_consist(sdp)\
gfs_consist_i((sdp), FALSE, __FUNCTION__, __FILE__, __LINE__)
#define gfs_consist_cluster(sdp)\
gfs_consist_i((sdp), TRUE, __FUNCTION__, __FILE__, __LINE__)
void gfs_consist_i(struct gfs_sbd *sdp, int cluster_wide,
		   const char *function,
		   char *file, unsigned int line);

#define gfs_consist_inode(ip) \
gfs_consist_inode_i((ip), FALSE, __FUNCTION__, __FILE__, __LINE__)
#define gfs_consist_cluster_inode(ip) \
gfs_consist_inode_i((ip), TRUE, __FUNCTION__, __FILE__, __LINE__)
void gfs_consist_inode_i(struct gfs_inode *ip, int cluster_wide,
			 const char *function,
			 char *file, unsigned int line);

#define gfs_consist_rgrpd(rgd) \
gfs_consist_rgrpd_i((rgd), FALSE, __FUNCTION__, __FILE__, __LINE__)
#define gfs_consist_cluster_rgrpd(rgd) \
gfs_consist_rgrpd_i((rgd), TRUE, __FUNCTION__, __FILE__, __LINE__)
void gfs_consist_rgrpd_i(struct gfs_rgrpd *rgd, int cluster_wide,
			 const char *function,
			 char *file, unsigned int line);

#define gfs_io_error(sdp) \
gfs_io_error_i((sdp), __FUNCTION__, __FILE__, __LINE__);
void gfs_io_error_i(struct gfs_sbd *sdp,
		    const char *function,
		    char *file, unsigned int line);

#define gfs_io_error_inode(ip) \
gfs_io_error_inode_i((ip), __FUNCTION__, __FILE__, __LINE__);
void gfs_io_error_inode_i(struct gfs_inode *ip,
			  const char *function,
			  char *file, unsigned int line);

#define gfs_io_error_bh(sdp, bh) \
gfs_io_error_bh_i((sdp), (bh), __FUNCTION__, __FILE__, __LINE__);
void gfs_io_error_bh_i(struct gfs_sbd *sdp, struct buffer_head *bh,
		       const char *function,
		       char *file, unsigned int line);


/* Translate old asserts into new ones (for now) */

#define GFS_ASSERT_SBD(assertion, sdp, todo) gfs_assert((sdp), (assertion), todo)
#define GFS_ASSERT_GLOCK(assertion, gl, todo) gfs_assert((gl)->gl_sbd, (assertion), todo)
#define GFS_ASSERT_INODE(assertion, ip, todo) gfs_assert((ip)->i_sbd, (assertion), todo)
#define GFS_ASSERT_RGRPD(assertion, rgd, todo) gfs_assert((rgd)->rd_sbd, (assertion), todo)



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
