/******************************************************************************
*******************************************************************************
**
**  Copyright (C) 2004-2006 Red Hat, Inc.  All rights reserved.
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
#include "service.h"
#include "config.h"
#include "../lcr/lcr_comp.h"


#include "cnxman-socket.h"
#include "logging.h"
#include "swab.h"
#include "ais.h"
#include "cmanccs.h"
#include "daemon.h"

extern int our_nodeid();
extern char cluster_name[MAX_CLUSTER_NAME_LEN+1];
extern int ip_port;
extern char *key_filename;
extern unsigned int quorumdev_poll;
extern unsigned int shutdown_timeout;
extern int init_config(struct objdb_iface_ver0 *objdb);

struct totem_ip_address mcast_addr;
struct totem_ip_address ifaddrs[MAX_INTERFACES];
int num_interfaces;
uint64_t incarnation;

static char errorstring[512];
static unsigned int debug_mask;
static struct objdb_iface_ver0 *global_objdb;
static totempg_groups_handle group_handle;
static struct totempg_group cman_group[1] = {
        { .group          = "CMAN", .group_len      = 4},
};

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

static int comms_init_ais(struct objdb_iface_ver0 *objdb);
static void cman_deliver_fn(struct totem_ip_address *source_addr, struct iovec *iovec, int iov_len,
			    int endian_conversion_required);
static void cman_confchg_fn(enum totem_configuration_type configuration_type,
			    struct totem_ip_address *member_list, int member_list_entries,
			    struct totem_ip_address *left_list, int left_list_entries,
			    struct totem_ip_address *joined_list, int joined_list_entries,
			    struct memb_ring_id *ring_id);
static int cman_readconfig (struct objdb_iface_ver0 *objdb, char **error_string);

/* Plugin-specific code */
/* Need some better way of determining these.... */
#define CMAN_SERVICE 8

#define LOG_SERVICE LOG_SERVICE_AMF
#include "print.h"
#define LOG_LEVEL_FROM_LIB LOG_LEVEL_DEBUG
#define LOG_LEVEL_FROM_GMI LOG_LEVEL_DEBUG
#define LOG_LEVEL_ENTER_FUNC LOG_LEVEL_DEBUG

static int cman_exit_fn (void *conn_info);
static int cman_exec_init_fn (struct objdb_iface_ver0 *objdb);

/* These just makes the code below a little neater */
static inline int objdb_get_string(struct objdb_iface_ver0 *objdb, unsigned int object_service_handle,
				   char *key, char **value)
{
	int res;

	*value = NULL;
	if ( !(res = objdb->object_key_get (object_service_handle,
					    key,
					    strlen (key),
					    (void *)value,
					    NULL))) {
		if (*value)
			return 0;
	}
	return -1;
}

static inline void objdb_get_int(struct objdb_iface_ver0 *objdb, unsigned int object_service_handle,
				   char *key, unsigned int *intvalue)
{
	char *value = NULL;

	if (!objdb->object_key_get (object_service_handle,
				    key,
				    strlen (key),
				    (void *)&value,
				    NULL)) {
		if (value) {
			*intvalue = atoi(value);
		}
	}
}


/*
 * Exports the interface for the service
 */
static struct openais_service_handler cman_service_handler = {
	.name		    		= (unsigned char *)"openais CMAN membership service 2.01",
	.id			        = CMAN_SERVICE,
	.lib_exit_fn		       	= cman_exit_fn,
	.exec_init_fn		       	= cman_exec_init_fn,
	.config_init_fn                 = NULL,
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
		.interfaces		= NULL,
	}
};

static struct lcr_comp cman_comp_ver0 = {
	.iface_count			= 1,
	.ifaces			       	= openais_cman_ver0
};


static struct openais_service_handler *cman_get_handler_ver0 (void)
{
	return (&cman_service_handler);
}


__attribute__ ((constructor)) static void cman_comp_register (void) {
	lcr_interfaces_set (&openais_cman_ver0[0], &cman_service_handler_iface);
	lcr_component_register (&cman_comp_ver0);
}

/* ------------------------------- */
/* Code for configuration plugin */
static struct config_iface_ver0 cmanconfig_iface_ver0 = {
	.config_readconfig        = cman_readconfig
};

static struct lcr_iface cmanconfig_ver0[1] = {
	{
		.name				= "cmanconfig",
		.version			= 0,
		.versions_replace		= 0,
		.versions_replace_count		= 0,
		.dependencies			= 0,
		.dependency_count		= 0,
		.constructor			= NULL,
		.destructor			= NULL,
		.interfaces			= NULL,
	}
};

static struct lcr_comp cmanconfig_comp_ver0 = {
	.iface_count				= 1,
	.ifaces					= cmanconfig_ver0
};



__attribute__ ((constructor)) static void cmanconfig_comp_register (void) {
	lcr_interfaces_set (&cmanconfig_ver0[0], &cmanconfig_iface_ver0);
	lcr_component_register (&cmanconfig_comp_ver0);
}

/* ------------------------------- */

static int cman_readconfig (struct objdb_iface_ver0 *objdb, char **error_string)
{
	int error;

	/* Initialise early logging */
	if (getenv("CMAN_DEBUGLOG"))
	{
		debug_mask = atoi(getenv("CMAN_DEBUGLOG"));

		log_setup(NULL, LOG_MODE_STDERR|LOG_MODE_DEBUG, "/tmp/ais");
		init_debug(debug_mask);
	}
	else
	{
		log_setup(NULL, LOG_MODE_SYSLOG, "/tmp/ais");
		init_debug(0);
		debug_mask = 0;
	}

	global_objdb = objdb;

	/* Read low-level totem/aisexec etc config from CCS */
	init_config(objdb);

	/* Read cman-specific config from CCS */
	error = read_ccs_config();
	if (error)
	{
		sprintf(errorstring, "Error reading config from CCS");
		return -1;
	}

	/* Do config overrides */
	comms_init_ais(objdb);

	return 0;
}

static int cman_exec_init_fn (struct objdb_iface_ver0 *objdb)
{
	unsigned int object_handle;

	/* Get our config variable */
	objdb->object_find_reset(OBJECT_PARENT_HANDLE);
	if (objdb->object_find(OBJECT_PARENT_HANDLE,
			       "cman",
			       strlen ("cman"),
			       &object_handle) == 0)
	{
		objdb_get_int(objdb, object_handle, "quorum_dev_poll", &quorumdev_poll);
		objdb_get_int(objdb, object_handle, "shutdown_timeout", &shutdown_timeout);

		/* Only use the CCS version of this if it was not overridden on the command-line */
		if (!getenv("CMAN_DEBUGLOG"))
		{
			objdb_get_int(objdb, object_handle, "debug_mask", &debug_mask);
			init_debug(debug_mask);
		}
	}

	/* Open local sockets and initialise I/O queues */
	cman_init();

	/* Start totem */
	totempg_groups_initialize(&group_handle, cman_deliver_fn, cman_confchg_fn);
	totempg_groups_join(group_handle, cman_group, 1);

	return (0);
}


int cman_exit_fn (void *conn_info)
{
	cman_finish();
	return (0);
}

/* END Plugin-specific code */

int ais_add_ifaddr(char *mcast, char *ifaddr, int portnum)
{
	unsigned int totem_object_handle;
	unsigned int interface_object_handle;
	char tmp[132];
	int ret = -1;

	P_AIS("Adding local address %s\n", ifaddr);


	/* First time, save the multicast address */
	if (!num_interfaces)
	{
		ret = totemip_parse(&mcast_addr, mcast);
		if (ret)
			return ret;
	}

	/* This will already exist as early config creates it */
	global_objdb->object_find_reset (OBJECT_PARENT_HANDLE);
	if (global_objdb->object_find (
		OBJECT_PARENT_HANDLE,
		"totem",
		strlen ("totem"),
		&totem_object_handle) == 0) {

		if (global_objdb->object_create(totem_object_handle, &interface_object_handle,
						"interface", strlen ("interface")) == 0) {

			P_AIS("Setting if %d, name: %s,  mcast: %s,  port=%d, \n",
			      num_interfaces, ifaddr, mcast, ip_port);
			sprintf(tmp, "%d", num_interfaces);
			global_objdb->object_key_create(interface_object_handle, "ringnumber", strlen("ringnumber"),
							tmp, strlen(tmp)+1);

			global_objdb->object_key_create(interface_object_handle, "bindnetaddr", strlen("bindnetaddr"),
							ifaddr, strlen(ifaddr)+1);

			global_objdb->object_key_create(interface_object_handle, "mcastaddr", strlen("mcastaddr"),
							mcast, strlen(mcast)+1);

			sprintf(tmp, "%d", ip_port);
			global_objdb->object_key_create(interface_object_handle, "mcastport", strlen ("mcastport"),
							tmp, strlen(tmp)+1);


			/* Save a local copy */
			ret = totemip_parse(&ifaddrs[num_interfaces], ifaddr);
			if (!ret)
				num_interfaces++;
			else
				return ret;

		}
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
static void cman_deliver_fn(struct totem_ip_address *source_addr, struct iovec *iovec, int iov_len,
			    int endian_conversion_required)
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

/* These are basically our overrides to the totem config bits */
static int comms_init_ais(struct objdb_iface_ver0 *objdb)
{
	unsigned int object_handle;
	char tmp[256];

	P_AIS("comms_init_ais()\n");

	objdb->object_find_reset(OBJECT_PARENT_HANDLE);

	if (objdb->object_find(OBJECT_PARENT_HANDLE,
			       "totem",
			       strlen ("totem"),
			       &object_handle) == 0)
	{
		objdb->object_key_create(object_handle, "version", strlen("version"),
					 "2", 2);


		sprintf(tmp, "%d", our_nodeid());
		objdb->object_key_create(object_handle, "nodeid", strlen ("nodeid"),
					 tmp, strlen(tmp)+1);

		sprintf(tmp, "%d", 1);
		objdb->object_key_create(object_handle, "secauth", strlen ("secauth"),
					 tmp, strlen(tmp)+1);

		if (key_filename)
		{
			objdb->object_key_create(object_handle, "keyfile", strlen ("keyfile"),
						 key_filename, strlen(key_filename)+1);
		}
		else /* Use the cluster name as key, this allows us to isolate different clusters on a single network */
		{
			int keylen;
			memset(tmp, 0, sizeof(tmp));

			strcpy(tmp, cluster_name);

			/* Key length must be a multiple of 4 */
			keylen = (strlen(cluster_name)+4) & 0xFC;
			objdb->object_key_create(object_handle, "key", strlen ("key"),
						 tmp, keylen);
		}
	}

	/* Make sure mainconfig doesn't stomp on our logging options */
	objdb->object_find_reset(OBJECT_PARENT_HANDLE);
	if (objdb->object_find(OBJECT_PARENT_HANDLE,
			       "logging",
			       strlen ("logging"),
			       &object_handle) == 0)
	{
		if (debug_mask)
		{
			objdb->object_key_create(object_handle, "to_stderr", strlen ("to_stderr"),
						 "yes", strlen("yes")+1);
			objdb->object_key_create(object_handle, "debug", strlen ("debug"),
						 "on", strlen("on")+1);
		}
		else
		{
			objdb->object_key_create(object_handle, "to_syslog", strlen ("to_syslog"),
						 "yes", strlen("yes")+1);
		}
	}

	/* Don't run under user "ais" */
	objdb->object_find_reset(OBJECT_PARENT_HANDLE);
	if (objdb->object_find(OBJECT_PARENT_HANDLE,
			       "aisexec",
			       strlen ("aisexec"),
			       &object_handle) == 0)
	{
		objdb->object_key_create(object_handle, "user", strlen ("user"),
				 "root", strlen ("root") + 1);
		objdb->object_key_create(object_handle, "group", strlen ("group"),
				 "root", strlen ("root") + 1);
	}

	/* Make sure we load our alter-ego */
	objdb->object_create(OBJECT_PARENT_HANDLE, &object_handle,
			     "service", strlen ("service"));
	objdb->object_key_create(object_handle, "name", strlen ("name"),
				 "openais_cman", strlen ("openais_cman") + 1);
	objdb->object_key_create(object_handle, "ver", strlen ("ver"),
				 "0", 2);

	return 0;
}
