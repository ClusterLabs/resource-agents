#ifndef __EATTR_DOT_H__
#define __EATTR_DOT_H__

#define GFS_EA_REC_LEN(ea) gfs32_to_cpu((ea)->ea_rec_len)
#define GFS_EA_DATA_LEN(ea) gfs32_to_cpu((ea)->ea_data_len)

#define GFS_EA_SIZE(ea) \
MAKE_MULT8(sizeof(struct gfs_ea_header) + \
	   (ea)->ea_name_len + \
	   ((GFS_EA_IS_STUFFED(ea)) ? \
	    GFS_EA_DATA_LEN(ea) : \
	    (sizeof(uint64_t) * (ea)->ea_num_ptrs)))
#define GFS_EA_STRLEN(ea) \
((((ea)->ea_type == GFS_EATYPE_USR) ? 5 : 7) + \
 (ea)->ea_name_len + 1)

#define GFS_EA_IS_STUFFED(ea) (!(ea)->ea_num_ptrs)
#define GFS_EA_IS_LAST(ea) ((ea)->ea_flags & GFS_EAFLAG_LAST)

#define GFS_EAREQ_SIZE_STUFFED(er) \
MAKE_MULT8(sizeof(struct gfs_ea_header) + \
	   (er)->er_name_len + (er)->er_data_len)
#define GFS_EAREQ_SIZE_UNSTUFFED(sdp, er) \
MAKE_MULT8(sizeof(struct gfs_ea_header) + \
	   (er)->er_name_len + \
	   sizeof(uint64_t) * DIV_RU((er)->er_data_len, (sdp)->sd_jbsize))

#define GFS_EA2NAME(ea) ((char *)((struct gfs_ea_header *)(ea) + 1))
#define GFS_EA2DATA(ea) (GFS_EA2NAME(ea) + (ea)->ea_name_len)
#define GFS_EA2DATAPTRS(ea) \
((uint64_t *)(GFS_EA2NAME(ea) + MAKE_MULT8((ea)->ea_name_len)))
#define GFS_EA2NEXT(ea) \
((struct gfs_ea_header *)((char *)(ea) + GFS_EA_REC_LEN(ea)))
#define GFS_EA_BH2FIRST(bh) \
((struct gfs_ea_header *)((bh)->b_data + \
			  sizeof(struct gfs_meta_header)))

struct gfs_ea_request {
	char *er_name;
	char *er_data;
	unsigned int er_name_len;
	unsigned int er_data_len;
	unsigned int er_type; /* GFS_EATYPE_... */
	int er_flags;
	mode_t er_mode;
};

struct gfs_ea_location {
	struct buffer_head *el_bh;
	struct gfs_ea_header *el_ea;
	struct gfs_ea_header *el_prev;
};

static inline unsigned int
gfs_ea_strlen(struct gfs_ea_header *ea)
{
	switch (ea->ea_type) {
	case GFS_EATYPE_USR:
		return (5 + (ea->ea_name_len + 1));
	case GFS_EATYPE_SYS:
		return (7 + (ea->ea_name_len + 1));
	case GFS_EATYPE_SECURITY:
		return (9 + (ea->ea_name_len + 1));
	default:
		return (0);
	}
}

int gfs_ea_repack(struct gfs_inode *ip);

int gfs_ea_get_i(struct gfs_inode *ip, struct gfs_ea_request *er);
int gfs_ea_set_i(struct gfs_inode *ip, struct gfs_ea_request *er);
int gfs_ea_remove_i(struct gfs_inode *ip, struct gfs_ea_request *er);

int gfs_ea_list(struct gfs_inode *ip, struct gfs_ea_request *er);
int gfs_ea_get(struct gfs_inode *ip, struct gfs_ea_request *er);
int gfs_ea_set(struct gfs_inode *ip, struct gfs_ea_request *er);
int gfs_ea_remove(struct gfs_inode *ip, struct gfs_ea_request *er);

int gfs_ea_dealloc(struct gfs_inode *ip);

int gfs_get_eattr_meta(struct gfs_inode *ip, struct gfs_user_buffer *ub);

/* Exported to acl.c */

int gfs_ea_check_size(struct gfs_sbd *sdp, struct gfs_ea_request *er);
int gfs_ea_find(struct gfs_inode *ip,
		struct gfs_ea_request *er,
		struct gfs_ea_location *el);
int gfs_ea_get_copy(struct gfs_inode *ip,
		    struct gfs_ea_location *el,
		    char *data);
int gfs_ea_acl_init(struct gfs_inode *ip, struct gfs_ea_request *er);
int gfs_ea_acl_chmod(struct gfs_inode *ip, struct gfs_ea_location *el,
		     struct iattr *attr, char *data);

#endif /* __EATTR_DOT_H__ */
