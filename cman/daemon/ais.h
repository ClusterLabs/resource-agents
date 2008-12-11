#include <corosync/engine/quorum.h>
/* DLM Currently maxes out at 3 ! */
#define MAX_INTERFACES 8

extern int ais_add_ifaddr(char *mcast, char *ifaddr, int portnum);
extern int comms_send_message(void *buf, int len,
			      unsigned char toport, unsigned char fromport,
			      int nodeid,
			      unsigned int flags);
extern uint64_t incarnation;
extern int num_ais_nodes;
extern quorum_set_quorate_fn_t corosync_set_quorum;
extern struct memb_ring_id cman_ring_id;
