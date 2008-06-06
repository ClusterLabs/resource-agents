#ifndef __DAEMON_DOT_H__
#define __DAEMON_DOT_H__

int gfs_scand(void *data);
int gfs_glockd(void *data);
int gfs_recoverd(void *data);
int gfs_logd(void *data);
int gfs_quotad(void *data);
int gfs_inoded(void *data);

#endif /* __DAEMON_DOT_H__ */
