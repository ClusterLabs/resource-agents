/* DLM Currently maxes out at 3 ! */
#define MAX_INTERFACES 8

#include <openais/totem/totem.h>

extern int ais_add_ifaddr(char *mcast, char *ifaddr, int portnum);
extern int comms_send_message(void *buf, int len,
			      unsigned char toport, unsigned char fromport,
			      int nodeid,
			      unsigned int flags);
extern uint64_t incarnation;
extern int num_ais_nodes;
