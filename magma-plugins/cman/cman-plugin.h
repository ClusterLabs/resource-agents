#ifndef __CMAN_PLUGIN_H
#define __CMAN_PLUGIN_H

typedef struct {
	int	sockfd;
	int	quorum_state;
	int	memb_count;
	uint64_t memb_sum;
} cman_priv_t;

#endif /* __CMAN_PLUGIN_H */
