#ifndef __LIBLM_H
#define __LIBLM_H

/*
 * Lock range structure
 * Must match gdlm_range_t, but redefined here so that
 * userland users don't need to include all the gdlm stuff.
 */

typedef struct lock_range
{
    uint64_t ra_start;
    uint64_t ra_end;
} lock_range_t;

/* Functions in libdlm.a */
extern int lock_resource(const char *resource, int mode, int flags, int *lockid);
extern int lock_subresource(const char *resource, int mode, int flags,
	                    int parent, int *lockid);
extern int lock_range(const char *resource, int mode, int flags, int *lockid,
		      int parent, lock_range_t *range);
extern int unlock_resource(const char *resource, int lockid);
extern int query_resource_holder(const char *resource, char *node, int *mode);

/* Lock modes: */
#define   LKM_NLMODE      0               /* null lock */
#define   LKM_CRMODE      1               /* concurrent read */
#define   LKM_CWMODE      2               /* concurrent write */
#define   LKM_PRMODE      3               /* protected read */
#define   LKM_PWMODE      4               /* protected write */
#define   LKM_EXMODE      5               /* exclusive */


/* Locking flags - these match the ones
 * in gdlm.h
 */
#define LKF_NONBLOCK    0x01
#define LKF_CONVERT     0x04
#define LKF_PERSIST     0x80


#endif
