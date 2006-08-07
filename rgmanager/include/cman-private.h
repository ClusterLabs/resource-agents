#ifndef _CMAN_PRIVATE_H
#define _CMAN_PRIVATE_H

#include <libcman.h>

int cman_init_subsys(cman_handle_t ch);
cman_handle_t cman_lock(int block, int sig);
cman_handle_t cman_lock_preemptible(int block, int *fd);
int cman_cleanup_subsys(void);
int cman_unlock(cman_handle_t ch);
int cman_send_data_unlocked(void *buf, int len, int flags,
			    uint8_t port, int nodeid);

#endif
