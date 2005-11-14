#include <sys/poll.h>
#include <sys/types.h>
#include <sys/errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <syslog.h>
#include <sys/socket.h>
#include <sys/utsname.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>

#include "cnxman-socket.h"

#include "totemip.h"
#include "totemconfig.h"
#include "commands.h"
#include "totempg.h"
#include "aispoll.h"
#include "logging.h"
#include "swab.h"
#include "ais.h"
#include "config.h"

extern int our_nodeid();

uint64_t incarnation;
struct totem_ip_address mcast_addr;
struct totem_interface ifaddrs[MAX_INTERFACES];
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
	int ret;

	P_AIS("Adding multi address %s\n", mcast);
	ret = totemip_parse(&mcast_addr, mcast);

	if (num_interfaces && mcast_addr.family != ifaddrs[0].bindnet.family) {
		P_AIS("multicast address is not same family as host address\n");
		ret = -EINVAL;
	}
	return ret;
}

int ais_add_ifaddr(char *ifaddr)
{
	int ret;

	P_AIS("Adding local address %s\n", ifaddr);

	ret = totemip_parse(&ifaddrs[num_interfaces].bindnet, ifaddr);
	if (ret == 0) {
		int i;

		for (i=0; i<num_interfaces; i++) {
			if (ifaddrs[i].bindnet.family != ifaddrs[num_interfaces].bindnet.family) {
				P_AIS("new address is not same family as others\n");
				return -EINVAL;
			}
		}
		if (mcast_addr.family && mcast_addr.family != ifaddrs[num_interfaces].bindnet.family) {
			P_AIS("new address is not same family as multicast address\n");
			return -EINVAL;
		}
		num_interfaces++;
	}

	return ret;
}


int comms_send_message(void *buf, int len,
		       unsigned char toport, unsigned char fromport,
		       int nodeid,
		       unsigned int flags)
{
	struct iovec iov[2];
	struct cl_protheader header;
	int totem_flags = TOTEMPG_AGREED;

	P_AIS("comms send message %p len = %d\n", buf,len);
	header.tgtport = toport;
	header.srcport = fromport;
	header.flags   = flags;
	header.srcid   = our_nodeid();
	header.tgtid   = nodeid;

	iov[0].iov_base = &header;
	iov[0].iov_len  = sizeof(header);
	iov[1].iov_base = buf;
	iov[1].iov_len  = len;

	if (flags & MSG_TOTEM_SAFE)
		totem_flags = TOTEMPG_SAFE;

	return totempg_mcast(iov, 2, totem_flags);
}

// This assumes the iovec has only one element ... is it true ??
static void deliver_fn(struct totem_ip_address *source_addr, struct iovec *iovec, int iov_len, int endian_conversion_required)
{
	struct cl_protheader *header = iovec->iov_base;
	char *buf = iovec->iov_base;

	P_AIS("deliver_fn called, iov_len = %d, iov[0].len = %d, source=%s, conversion reqd=%d\n",
	      iov_len, iovec->iov_len, totemip_print(source_addr), endian_conversion_required);

	if (endian_conversion_required) {
		header->srcid = swab32(header->srcid);
		header->tgtid = swab32(header->tgtid);
		header->flags = swab32(header->flags);
	}
	
	/* Only pass on messages for us or everyone */
	if (header->tgtid == our_nodeid() ||
	    header->tgtid == 0) {
		send_to_userport(header->srcport, header->tgtport,
				 header->srcid, header->tgtid,
				 source_addr,
				 buf + sizeof(struct cl_protheader),
				 iovec->iov_len - sizeof(struct cl_protheader),
				 endian_conversion_required);
	}
}

static void confchg_fn(enum totem_configuration_type configuration_type,
		       struct totem_ip_address *member_list, int member_list_entries,
		       struct totem_ip_address *left_list, int left_list_entries,
		       struct totem_ip_address *joined_list, int joined_list_entries,
		       struct memb_ring_id *ring_id)
{
	int i;
	static int last_memb_count = 0;

	P_AIS("confchg_fn called type = %d, seq=%lld\n", configuration_type, ring_id->seq);

	incarnation = ring_id->seq;

	/* Tell the cman membership layer */
	for (i=0; i<left_list_entries; i++)
		del_ais_node(&left_list[i]);
	for (i=0; i<joined_list_entries; i++)
		add_ais_node(&joined_list[i], incarnation, member_list_entries);

	if (configuration_type == TOTEM_CONFIGURATION_REGULAR) {
		P_AIS("last memb_count = %d, current = %d\n", last_memb_count, member_list_entries);
		send_transition_msg(last_memb_count);
		last_memb_count = member_list_entries;
	}
}

extern poll_handle ais_poll_handle; // From daemon.c
int comms_init_ais(unsigned short port, char *key_filename)
{
	char *errstring;

	/* AIS doesn't like these to disappear */
	static struct totem_config ais_config;
	static totemsrp_handle totemsrp_handle_in;

	P_AIS("comms_init_ais()\n");

	ais_config.interfaces = ifaddrs;
	ais_config.interface_count = num_interfaces;
	ais_config.ip_port = htons(port);

	totemip_copy(&ais_config.mcast_addr, &mcast_addr);
	ais_config.node_id = htonl(our_nodeid());

	ais_config.totem_logging_configuration.log_printf = log_msg;
	ais_config.totem_logging_configuration.log_level_security = 5;
	ais_config.totem_logging_configuration.log_level_error = 4;
	ais_config.totem_logging_configuration.log_level_warning = 3;
	ais_config.totem_logging_configuration.log_level_notice = 2;
	ais_config.totem_logging_configuration.log_level_debug = 1;

        /* Set defaults */
	ais_config.token_retransmits_before_loss_const = cman_config[TOKEN_RETRANSMITS_BEFORE_LOSS_CONST].value;
	ais_config.token_timeout = cman_config[TOKEN_TIMEOUT].value;
	ais_config.token_retransmit_timeout = cman_config[TOKEN_RETRANSMIT_TIMEOUT].value;
	ais_config.token_hold_timeout = cman_config[TOKEN_HOLD_TIMEOUT].value;
	ais_config.join_timeout = cman_config[JOIN_TIMEOUT].value;
	ais_config.consensus_timeout = cman_config[CONSENSUS_TIMEOUT].value;
	ais_config.merge_timeout = cman_config[MERGE_TIMEOUT].value;
	ais_config.downcheck_timeout = cman_config[DOWNCHECK_TIMEOUT].value;
	ais_config.fail_to_recv_const = cman_config[FAIL_TO_RECV_CONST].value;
	ais_config.seqno_unchanged_const = cman_config[SEQNO_UNCHANGED_CONST].value;
	ais_config.net_mtu = 1500;
	ais_config.threads = 0;//2;

	// TEMP clear it all
	ais_config.totem_logging_configuration.log_level_security =
	ais_config.totem_logging_configuration.log_level_error =
	ais_config.totem_logging_configuration.log_level_warning =
	ais_config.totem_logging_configuration.log_level_notice =
	ais_config.totem_logging_configuration.log_level_debug = 1;

	if (key_filename)
	{
		ais_config.secauth = 1;
		P_AIS("Reading key from file %s\n", key_filename);
		if (totem_config_keyread(key_filename, &ais_config, &errstring))
		{
			P_AIS("Unable to read key from file %s: %s\n", key_filename, errstring);
			log_msg(LOG_ERR, "Unable to read key from file %s: %s\n", key_filename, errstring);
			exit(22);
		}
	}
	else
	{
		ais_config.secauth = 0;
		ais_config.private_key_len = 0;
	}

        totempg_initialize(ais_poll_handle,
			   &totemsrp_handle_in,
			   &ais_config,
			   deliver_fn, confchg_fn);

	return 0;
}
