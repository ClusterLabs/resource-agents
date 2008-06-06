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

/**
 * gfs_assert - Cause the machine to panic if @assertion is false
 * @sdp:
 * @assertion:
 * @todo:
 *
 */

void gfs_assert_i(struct gfs_sbd *sdp,
                  char *assertion,
                  const char *function,
                  char *file, unsigned int line)
__attribute__ ((noreturn));
#define gfs_assert(sdp, assertion, todo) \
do { \
	if (unlikely(!(assertion))) { \
		{todo} \
		gfs_assert_i((sdp), #assertion, \
			     __FUNCTION__, __FILE__, __LINE__); \
	} \
} while (0)

/**
 * gfs_assert_withdraw - Cause the machine to withdraw if @assertion is false
 * @sdp:
 * @assertion:
 *
 * Returns: 0 if things are ok,
 *          -1 if this call withdrew the machine,
 *          -2 if it was already withdrawn
 */

int gfs_assert_withdraw_i(struct gfs_sbd *sdp,
			  char *assertion,
			  const char *function,
			  char *file, unsigned int line);
#define gfs_assert_withdraw(sdp, assertion) \
((likely(assertion)) ? 0 : \
 gfs_assert_withdraw_i((sdp), #assertion, \
		       __FUNCTION__, __FILE__, __LINE__))

/**
 * gfs_assert_warn - Print a message to the console if @assertion is false
 * @sdp:
 * @assertion:
 *
 * Returns: 0 if things are ok,
 *          -1 if we printed something
 *          -2 if we didn't
 */

int gfs_assert_warn_i(struct gfs_sbd *sdp,
		      char *assertion,
		      const char *function,
		      char *file, unsigned int line);
#define gfs_assert_warn(sdp, assertion) \
((likely(assertion)) ? 0 : \
 gfs_assert_warn_i((sdp), #assertion, \
		   __FUNCTION__, __FILE__, __LINE__))

/**
 * gfs_consist - Flag a filesystem consistency error and withdraw
 * gfs_cconsist - Flag a filesystem consistency error and withdraw cluster
 * @sdp:
 *
 * Returns: -1 if this call withdrew the machine,
 *          0 if it was already withdrawn
 */

int gfs_consist_i(struct gfs_sbd *sdp, int cluster_wide,
		  const char *function,
		  char *file, unsigned int line);
#define gfs_consist(sdp)\
gfs_consist_i((sdp), FALSE, __FUNCTION__, __FILE__, __LINE__)
#define gfs_cconsist(sdp)\
gfs_consist_i((sdp), TRUE, __FUNCTION__, __FILE__, __LINE__)

/**
 * gfs_consist_inode - Flag an inode consistency error and withdraw
 * gfs_cconsist_inode - Flag an inode consistency error and withdraw cluster
 * @ip:
 *
 * Returns: -1 if this call withdrew the machine,
 *          0 if it was already withdrawn
 */

int gfs_consist_inode_i(struct gfs_inode *ip, int cluster_wide,
			const char *function,
			char *file, unsigned int line);
#define gfs_consist_inode(ip) \
gfs_consist_inode_i((ip), FALSE, __FUNCTION__, __FILE__, __LINE__)
#define gfs_cconsist_inode(ip) \
gfs_consist_inode_i((ip), TRUE, __FUNCTION__, __FILE__, __LINE__)

/**
 * gfs_consist_rgrpd - Flag a RG consistency error and withdraw
 * gfs_cconsist_rgrpd - Flag a RG consistency error and withdraw cluster
 * @rgd:
 *
 * Returns: -1 if this call withdrew the machine,
 *          0 if it was already withdrawn
 */

int gfs_consist_rgrpd_i(struct gfs_rgrpd *rgd, int cluster_wide,
			const char *function,
			char *file, unsigned int line);
#define gfs_consist_rgrpd(rgd) \
gfs_consist_rgrpd_i((rgd), FALSE, __FUNCTION__, __FILE__, __LINE__)
#define gfs_cconsist_rgrpd(rgd) \
gfs_consist_rgrpd_i((rgd), TRUE, __FUNCTION__, __FILE__, __LINE__)

/**
 * gfs_meta_check - Flag a magic number consistency error and withdraw
 * @sdp:
 * @bh:
 *
 * Returns: 0 if things are ok,
 *          -1 if this call withdrew the machine,
 *          -2 if it was already withdrawn
 */

int gfs_meta_check_ii(struct gfs_sbd *sdp, struct buffer_head *bh,
		      const char *function,
		      char *file, unsigned int line);
static __inline__ int
gfs_meta_check_i(struct gfs_sbd *sdp, struct buffer_head *bh,
		 const char *function,
		 char *file, unsigned int line)
{
	uint32_t magic;
	magic = ((struct gfs_meta_header *)(bh)->b_data)->mh_magic;
	magic = gfs32_to_cpu(magic);
	if (likely(magic == GFS_MAGIC))
		return 0;
	return gfs_meta_check_ii(sdp, bh, function, file, line);
}
#define gfs_meta_check(sdp, bh) \
gfs_meta_check_i((sdp), (bh), \
		 __FUNCTION__, __FILE__, __LINE__)

/**
 * gfs_metatype_check - Flag a metadata type consistency error and withdraw
 * @sdp:
 * @bh:
 * @type:
 *
 * Returns: 0 if things are ok,
 *          -1 if this call withdrew the machine,
 *          -2 if it was already withdrawn
 */

int gfs_metatype_check_ii(struct gfs_sbd *sdp, struct buffer_head *bh,
			  uint32_t type, uint32_t t,
			  const char *function,
			  char *file, unsigned int line);
static __inline__ int
gfs_metatype_check_i(struct gfs_sbd *sdp, struct buffer_head *bh,
		     uint32_t type,
		     const char *function,
		     char *file, unsigned int line)
{
        uint32_t magic, t;
        magic = ((struct gfs_meta_header *)(bh)->b_data)->mh_magic;
        magic = gfs32_to_cpu(magic);
	if (unlikely(magic != GFS_MAGIC))
		return gfs_meta_check_ii(sdp, bh, function, file, line);
	t = ((struct gfs_meta_header *)(bh)->b_data)->mh_type;
	t = gfs32_to_cpu(t);
        if (unlikely(t != type))
		return gfs_metatype_check_ii(sdp, bh, type, t, function, file, line);
	return 0;
}
#define gfs_metatype_check(sdp, bh, type) \
gfs_metatype_check_i((sdp), (bh), (type), \
		     __FUNCTION__, __FILE__, __LINE__)

/**
 * gfs_metatype_check2 - Flag a metadata type consistency error and withdraw
 * @sdp:
 * @bh:
 * @type1:
 * @type2:
 *
 * Returns: 0 if things are ok,
 *          -1 if this call withdrew the machine,
 *          -2 if it was already withdrawn
 */

static __inline__ int
gfs_metatype_check2_i(struct gfs_sbd *sdp, struct buffer_head *bh,
		      uint32_t type1, uint32_t type2,
		      const char *function,
		      char *file, unsigned int line)
{
        uint32_t magic, t;
        magic = ((struct gfs_meta_header *)(bh)->b_data)->mh_magic;
        magic = gfs32_to_cpu(magic);
        if (unlikely(magic != GFS_MAGIC))
                return gfs_meta_check_ii(sdp, bh, function, file, line);
        t = ((struct gfs_meta_header *)(bh)->b_data)->mh_type;
        t = gfs32_to_cpu(t);
        if (unlikely(t != type1 && t != type2))
                return gfs_metatype_check_ii(sdp, bh, type1, t, function, file, line);
        return 0;
}
#define gfs_metatype_check2(sdp, bh, type1, type2) \
gfs_metatype_check2_i((sdp), (bh), (type1), (type2), \
                     __FUNCTION__, __FILE__, __LINE__)

/**
 * gfs_metatype_set - set the metadata type on a buffer
 * @bh:
 * @type:
 * @format:
 *
 */

static __inline__ void
gfs_metatype_set(struct buffer_head *bh, uint32_t type, uint32_t format)
{
	struct gfs_meta_header *mh;
	mh = (struct gfs_meta_header *)bh->b_data;
	mh->mh_type = cpu_to_gfs32(type);
	mh->mh_format = cpu_to_gfs32(format);
}

/**
 * gfs_io_error - Flag an I/O error and withdraw
 * @sdp:
 *
 * Returns: -1 if this call withdrew the machine,
 *          0 if it was already withdrawn
 */

int gfs_io_error_i(struct gfs_sbd *sdp,
		   const char *function,
		   char *file, unsigned int line);
#define gfs_io_error(sdp) \
gfs_io_error_i((sdp), __FUNCTION__, __FILE__, __LINE__);

/**
 * gfs_io_error_inode - Flag an inode I/O error and withdraw
 * @ip:
 *
 * Returns: -1 if this call withdrew the machine,
 *          0 if it was already withdrawn
 */

int gfs_io_error_inode_i(struct gfs_inode *ip,
			 const char *function,
			 char *file, unsigned int line);
#define gfs_io_error_inode(ip) \
gfs_io_error_inode_i((ip), __FUNCTION__, __FILE__, __LINE__);

/**
 * gfs_io_error_bh - Flag a buffer I/O error and withdraw
 * @sdp:
 * @bh:
 *
 * Returns: -1 if this call withdrew the machine,
 *          0 if it was already withdrawn
 */

int gfs_io_error_bh_i(struct gfs_sbd *sdp, struct buffer_head *bh,
		      const char *function,
		      char *file, unsigned int line);
#define gfs_io_error_bh(sdp, bh) \
gfs_io_error_bh_i((sdp), (bh), __FUNCTION__, __FILE__, __LINE__);


/* Memory stuff */

#define RETRY_MALLOC(do_this, until_this) \
for (;;) { \
	{ do_this; } \
	if (until_this) \
		break; \
	printk("GFS: out of memory: %s, %u\n", __FILE__, __LINE__); \
	dump_stack(); \
	yield(); \
}

extern struct kmem_cache *gfs_glock_cachep;
extern struct kmem_cache *gfs_inode_cachep;
extern struct kmem_cache *gfs_bufdata_cachep;
extern struct kmem_cache *gfs_mhc_cachep;

void *gmalloc(unsigned int size);


struct gfs_user_buffer {
	char *ub_data;
	unsigned int ub_size;
	unsigned int ub_count;
};
int gfs_add_bh_to_ub(struct gfs_user_buffer *ub, struct buffer_head *bh);


static __inline__ unsigned int
gfs_tune_get_i(struct gfs_tune *gt, unsigned int *p)
{
	unsigned int x;
	spin_lock(&gt->gt_spin);
	x = *p;
	spin_unlock(&gt->gt_spin);
	return x;
}
#define gfs_tune_get(sdp, field) \
gfs_tune_get_i(&(sdp)->sd_tune, &(sdp)->sd_tune.field)


#endif /* __UTIL_DOT_H__ */
