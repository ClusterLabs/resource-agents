#ifndef _CMAN_PLUGIN_H
#define _CMAN_PLUGIN_H

typedef struct {
	cman_handle_t handle;
	int	quorum_state;
	int	memb_count;
	uint64_t memb_sum;
	dlm_lshandle_t ls;
} cman_priv_t;

#endif /* _CMAN_PLUGIN_H */
