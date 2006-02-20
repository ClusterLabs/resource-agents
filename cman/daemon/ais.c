/******************************************************************************
*******************************************************************************
**
**  Copyright (C) 2004-2005 Red Hat, Inc.  All rights reserved.
**
**  This copyrighted material is made available to anyone wishing to use,
**  modify, copy, or redistribute it subject to the terms and conditions
**  of the GNU General Public License v.2.
**
*******************************************************************************
******************************************************************************/

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

#include "totemip.h"
#include "totemconfig.h"
#include "commands.h"
#include "totempg.h"
#include "aispoll.h"
#include "handlers.h"
#include "../lcr/lcr_comp.h"

#include "cnxman-socket.h"
#include "logging.h"
#include "swab.h"
#include "ais.h"
#include "cmanccs.h"
#include "daemon.h"
#include "config.h"

extern int our_nodeid();
extern char cluster_name[MAX_CLUSTER_NAME_LEN+1];
extern int ip_port;
extern char *key_filename;

uint64_t incarnation;
struct totem_ip_address mcast_addr;
struct totem_interface ifaddrs[MAX_INTERFACES];
int num_interfaces;
static totempg_groups_handle group_handle;
static struct totempg_group cman_group[1] = {
        { .group          = "CMAN", .group_len      = 4},
};

static struct totem_config cman_ais_config;

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

/* Plugin-specific code */
/* Need some better way of determining these.... */
#define CMAN_SERVICE 8

#define LOG_SERVICE LOG_SERVICE_AMF
#include "print.h"
#define LOG_LEVEL_FROM_LIB LOG_LEVEL_DEBUG
#define LOG_LEVEL_FROM_GMI LOG_LEVEL_DEBUG
#define LOG_LEVEL_ENTER_FUNC LOG_LEVEL_DEBUG

static int cman_exit_fn (void *conn_info);

static int cman_exec_init_fn (struct openais_config *);

static int cman_config_init_fn (struct openais_config *openais_config);

/*
 * Exports the interface for the service
 */
static struct openais_service_handler cman_service_handler = {
	.name		    		= (unsigned char *)"openais CMAN membership service 2.01",
	.id			        = CMAN_SERVICE,
	.lib_handlers	        	= NULL,
	.lib_handlers_count		= 0,
	.lib_exit_fn		       	= cman_exit_fn,
	.exec_handlers		        = NULL,
	.exec_handlers_count	        = 0,
	.exec_init_fn		       	= cman_exec_init_fn,
	.config_init_fn                 = cman_config_init_fn,
};

static struct openais_service_handler *cman_get_handler_ver0 (void);

static struct openais_service_handler_iface_ver0 cman_service_handler_iface = {
	.openais_get_service_handler_ver0 = cman_get_handler_ver0
};

static struct lcr_iface openais_cman_ver0[1] = {
	{
		.name		        = "openais_cman",
		.version	        = 0,
		.versions_replace     	= 0,
		.versions_replace_count = 0,
		.dependencies	      	= 0,
		.dependency_count      	= 0,
		.constructor	       	= NULL,
		.destructor		= NULL,
		.interfaces		= (void **)&cman_service_handler_iface,
	}
};

static struct lcr_comp cman_comp_ver0 = {
	.iface_count			= 1,
	.ifaces			       	= openais_cman_ver0
};

static void register_this_component (void) {
        lcr_component_register (&cman_comp_ver0);
}

void (*const __ctor_cman_comp[1]) (void) __attribute__ ((section(".ctors"))) = { register_this_component };


static struct openais_service_handler *cman_get_handler_ver0 (void)
{
	return (&cman_service_handler);
}

static int cman_config_init_fn (struct openais_config *openais_config)
{
	int error;

	init_config();
	error = read_ccs_config();
	if (error)
		exit(1);

	comms_init_ais();

	if (getenv("CMAN_DEBUGLOG"))
	    log_setup(NULL, LOG_MODE_STDERR, "/tmp/ais");

	memcpy(&openais_config->totem_config, &cman_ais_config, sizeof(struct totem_config));
	return 0;
}

static int cman_exec_init_fn (struct openais_config *openais_config)
{
	cman_init();
	return (0);
}


int cman_exit_fn (void *conn_info)
{
	cman_finish();
	return (0);
}

/* END Plugin-specific code */

int ais_set_mcast(char *mcast)
{
	int ret;

	P_AIS("Adding multi address %s\n", mcast);
	ret = totemip_parse(&mcast_addr, mcast);

	if (num_interfaces && mcast_addr.family != ifaddrs[0].bindnet.family) {
		log_msg(LOG_ERR, "multicast address '%s' is not same family as host address\n", mcast);
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
				log_msg(LOG_ERR, "new address is not same family as others\n");
				return -EINVAL;
			}
		}
		if (mcast_addr.family && mcast_addr.family != ifaddrs[num_interfaces].bindnet.family) {
			log_msg(LOG_ERR, "new address is not same family as multicast address\n");
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

	return totempg_groups_mcast_joined(group_handle, iov, 2, totem_flags);
}

// This assumes the iovec has only one element ... is it true ??
static void cman_deliver_fn(struct totem_ip_address *source_addr, struct iovec *iovec, int iov_len, int endian_conversion_required)
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

static void cman_confchg_fn(enum totem_configuration_type configuration_type,
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

int comms_init_ais()
{
	char *errstring;

	P_AIS("comms_init_ais()\n");

	cman_ais_config.interfaces = ifaddrs;
	cman_ais_config.interface_count = num_interfaces;
	cman_ais_config.ip_port = htons(ip_port);

	totemip_copy(&cman_ais_config.mcast_addr, &mcast_addr);
	cman_ais_config.node_id = our_nodeid();

	cman_ais_config.totem_logging_configuration.log_printf = log_msg;
	cman_ais_config.totem_logging_configuration.log_level_security = 5;
	cman_ais_config.totem_logging_configuration.log_level_error = 4;
	cman_ais_config.totem_logging_configuration.log_level_warning = 3;
	cman_ais_config.totem_logging_configuration.log_level_notice = 2;
	cman_ais_config.totem_logging_configuration.log_level_debug = 1;

        /* Set defaults */
	cman_ais_config.token_retransmits_before_loss_const = cman_config[TOKEN_RETRANSMITS_BEFORE_LOSS_CONST].value;
	cman_ais_config.token_timeout = cman_config[TOKEN_TIMEOUT].value;
	cman_ais_config.token_retransmit_timeout = cman_config[TOKEN_RETRANSMIT_TIMEOUT].value;
	cman_ais_config.token_hold_timeout = cman_config[TOKEN_HOLD_TIMEOUT].value;
	cman_ais_config.join_timeout = cman_config[JOIN_TIMEOUT].value;
	cman_ais_config.consensus_timeout = cman_config[CONSENSUS_TIMEOUT].value;
	cman_ais_config.merge_timeout = cman_config[MERGE_TIMEOUT].value;
	cman_ais_config.downcheck_timeout = cman_config[DOWNCHECK_TIMEOUT].value;
	cman_ais_config.fail_to_recv_const = cman_config[FAIL_TO_RECV_CONST].value;
	cman_ais_config.seqno_unchanged_const = cman_config[SEQNO_UNCHANGED_CONST].value;
	cman_ais_config.net_mtu = 1500;
	cman_ais_config.threads = cman_config[THREAD_COUNT].value;

	cman_ais_config.totem_logging_configuration.log_level_security =
	cman_ais_config.totem_logging_configuration.log_level_error =
	cman_ais_config.totem_logging_configuration.log_level_warning =
	cman_ais_config.totem_logging_configuration.log_level_notice =
	cman_ais_config.totem_logging_configuration.log_level_debug = cman_config[DEBUG_LEVEL].value;

	if (key_filename)
	{
		cman_ais_config.secauth = 1;
		P_AIS("Reading key from file %s\n", key_filename);
		if (totem_config_keyread(key_filename, &cman_ais_config, &errstring))
		{
			P_AIS("Unable to read key from file %s: %s\n", key_filename, errstring);
			log_msg(LOG_ERR, "Unable to read key from file %s: %s\n", key_filename, errstring);
			exit(22);
		}
	}
	else
	{
		cman_ais_config.secauth = 0;
		cman_ais_config.private_key_len = 0;
	}
	totempg_groups_initialize(&group_handle, cman_deliver_fn, cman_confchg_fn);

	totempg_groups_join(group_handle, cman_group, 1);

	return 0;
}
