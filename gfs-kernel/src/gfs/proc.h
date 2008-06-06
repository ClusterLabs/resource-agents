#ifndef __PROC_DOT_H__
#define __PROC_DOT_H__

/* Allow args to be passed to GFS when using an initial ram disk */
extern char *gfs_proc_margs;
extern spinlock_t gfs_proc_margs_lock;

void gfs_proc_fs_add(struct gfs_sbd *sdp);
void gfs_proc_fs_del(struct gfs_sbd *sdp);

int gfs_proc_init(void);
void gfs_proc_uninit(void);

#endif /* __PROC_DOT_H__ */
