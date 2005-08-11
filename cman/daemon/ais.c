#include <sys/poll.h>
#include <sys/types.h>
#include <sys/errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/utsname.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>

#include "commands.h"
#include "totempg.h"
#include "aispoll.h"
#include "logging.h"
extern int our_nodeid();

#define MAX_INTERFACES 16

static int initial_msg_sent;

uint64_t incarnation;

static char mcast_addr[128];
static struct totem_interface ifaddrs[MAX_INTERFACES]; // TODO make IPv6 aware...
       int num_interfaces;

/* This structure is tacked onto the start of a cluster message packet for our
 * own nefarious purposes. */
struct cl_protheader {
	unsigned char  tgtport; /* Target port number */
	unsigned char  srcport; /* Source (originating) port number */
	unsigned short pad;
	unsigned int   flags;
	int            srcid;	/* Node ID of the sender */
	int            tgtid;	/* Node ID of the target */
};


int ais_set_mcast(char *mcast)
{
	if (strlen(mcast) >= sizeof(mcast_addr))
		return -EINVAL;

	strcpy(mcast_addr, mcast);

	P_AIS("Adding multi address %s\n", mcast);
	return 0;
}

int ais_add_ifaddr(char *ifaddr)
{
        struct addrinfo *ainfo;
        struct addrinfo ahints;
	struct sockaddr_in *sa;
	int ret;

        memset(&ahints, 0, sizeof(ahints));
        ahints.ai_socktype = SOCK_DGRAM;
        ahints.ai_protocol = IPPROTO_UDP;

        /* Lookup the nodename address */
        ret = getaddrinfo(ifaddr, NULL, &ahints, &ainfo);

	P_AIS("Adding local address %s, ret = %d\n", ifaddr, ret);
	if (ret)
		return -errno;

	// TODO Make IPv6 Aware.
	sa = (struct sockaddr_in *)ainfo->ai_addr;
	ifaddrs[num_interfaces].bindnet.sin_family = AF_INET;
	ifaddrs[num_interfaces].bindnet.sin_port = htons(6809);
	ifaddrs[num_interfaces].bindnet.sin_addr.s_addr = sa->sin_addr.s_addr;

	num_interfaces++;

	return 0;
}


int comms_send_message(void *buf, int len,
		       unsigned char toport, unsigned char fromport,
		       int nodeid,
		       unsigned int flags)
{
	struct iovec iov[2];
	struct cl_protheader header;

	P_AIS("comms send message %p len = %d\n", buf,len);
	header.tgtport = toport;
	header.srcport = fromport;
	header.flags   = flags; // TODO anything with these flags ??
	header.srcid   = our_nodeid();
	header.tgtid   = nodeid;

	iov[0].iov_base = &header;
	iov[0].iov_len  = sizeof(header);
	iov[1].iov_base = buf;
	iov[1].iov_len  = len;

	// TODO TOTEMPG_AGREE/SAFE should selectable with flags.
	return totempg_mcast(iov, 2, TOTEMPG_AGREED);
}

// This assumes the iovec has only one element ... is it true ??
static void deliver_fn(struct in_addr source_addr, struct iovec *iovec, int iov_len, int endian_conversion_required)
{
	struct cl_protheader *header = iovec->iov_base;
	char *buf = iovec->iov_base;

	P_AIS("deliver_fn called, iov_len = %d, iov[0].len = %d, source=%s, conversion reqd=%d\n",
	      iov_len, iovec->iov_len, inet_ntoa(source_addr), endian_conversion_required);

	/* Only pass on messages for us or everyone */
	if (header->tgtid == our_nodeid() ||
	    header->tgtid == -1 ||  /* Joining node */
	    header->tgtid == 0) {   /* Broadcast */
		send_to_userport(header->srcport, header->tgtport,
				 header->srcid, header->tgtid,
				 source_addr.s_addr,
				 buf + sizeof(struct cl_protheader),
				 iovec->iov_len - sizeof(struct cl_protheader),
				 endian_conversion_required);
	}
}

static void confchg_fn(enum totem_configuration_type configuration_type,
		       struct in_addr *member_list, int member_list_entries,
		       struct in_addr *left_list, int left_list_entries,
		       struct in_addr *joined_list, int joined_list_entries,
		       struct memb_ring_id *ring_id)
{
	int i;

	P_AIS("confchg_fn called type = %d, seq=%lld\n", configuration_type, ring_id->seq);

	incarnation = ring_id->seq;

	if (!initial_msg_sent &&
	    configuration_type == TOTEM_CONFIGURATION_REGULAR &&
	    member_list_entries >= 1)
	{
		/* We actually send two NODEMSG messages, one when we first start up and are in a
		   single-node configuration and again when we know there are other nodes in the cluster.
		   This is because AIS does cluster "merges", all nodes start as a single-node ring and get
		   merged into the main one. This way we get to be a cluster member twice!
		*/
		send_joinreq();
		if (member_list_entries > 1)
			initial_msg_sent = 1;
	}

	/* Tell the cman membership layer */
	for (i=0; i<left_list_entries; i++)
		del_ais_node(left_list[i].s_addr);
	for (i=0; i<joined_list_entries; i++)
		add_ais_node(joined_list[i].s_addr, incarnation, member_list_entries);
}

extern poll_handle ais_poll_handle; // From daemon.c
int comms_init_ais(unsigned short port)
{
	int i;

	/* AIS doesn't like these to disappear */
	static struct totem_config cman_config;
	static totemsrp_handle totemsrp_handle_in;

	P_AIS("comms_init_ais()\n");

	for (i=0; i< num_interfaces; i++) {
		ifaddrs[i].bindnet.sin_port = htons(port);
	}

	cman_config.interfaces = ifaddrs;

	cman_config.interface_count = 1;
	cman_config.mcast_addr.sin_family = AF_INET;
	cman_config.mcast_addr.sin_port = htons(port);
	cman_config.mcast_addr.sin_addr.s_addr = inet_addr(mcast_addr);
	cman_config.private_key_len = 0;
	memset(&cman_config.timeouts, 0, sizeof(cman_config.timeouts));
	cman_config.totem_logging_configuration.log_printf = log_msg;
	cman_config.totem_logging_configuration.log_level_security = 5;
	cman_config.totem_logging_configuration.log_level_error = 4;
	cman_config.totem_logging_configuration.log_level_warning = 3;
	cman_config.totem_logging_configuration.log_level_notice = 2;
	cman_config.totem_logging_configuration.log_level_debug = 1;

	// TEMP clear it all
	cman_config.totem_logging_configuration.log_level_security =
	cman_config.totem_logging_configuration.log_level_error =
	cman_config.totem_logging_configuration.log_level_warning =
	cman_config.totem_logging_configuration.log_level_notice =
	cman_config.totem_logging_configuration.log_level_debug = 0;

        totempg_initialize(ais_poll_handle,
			   &totemsrp_handle_in,
			   &cman_config,
			   deliver_fn, confchg_fn);

	return 0;
}
