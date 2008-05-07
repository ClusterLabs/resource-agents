/******************************************************************************
*******************************************************************************
**
**  Copyright (C) 2007-2008 Red Hat, Inc.  All rights reserved.
**
**  This copyrighted material is made available to anyone wishing to use,
**  modify, copy, or redistribute it subject to the terms and conditions
**  of the GNU General Public License v.2.
**
*******************************************************************************
******************************************************************************/
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <assert.h>
#include <sys/utsname.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/errno.h>
#include <netdb.h>
#include <ifaddrs.h>
#include <arpa/inet.h>

#define DEFAULT_PORT            5405
#define DEFAULT_CLUSTER_NAME    "RHCluster"
#define NOCCS_KEY_FILENAME      "/etc/cluster/cman_authkey"

/* openais headers */
#include <openais/service/objdb.h>
#include <openais/service/swab.h>
#include <openais/service/service.h>
#include <openais/service/config.h>
#include <openais/lcr/lcr_comp.h>
#include <openais/service/swab.h>

#include "cman.h"
#include "cmanconfig.h"
#include "cnxman-socket.h"
#include "nodelist.h"
#include "logging.h"

#define MAX_PATH_LEN PATH_MAX

static unsigned int debug_mask;
static int cmanpre_readconfig(struct objdb_iface_ver0 *objdb, char **error_string);

static char *nodename_env;
static int expected_votes;
static int votes;
static int num_interfaces;
static int startup_pipe;
static unsigned int cluster_id;
static char nodename[MAX_CLUSTER_MEMBER_NAME_LEN];
static int nodeid;
static int two_node;
static unsigned int portnum;
static int num_nodenames;
static char *key_filename;
static char *mcast_name;
static char *cluster_name;
static char error_reason[1024] = { '\0' };
static unsigned int cluster_parent_handle;

/*
 * Exports the interface for the service
 */
static struct config_iface_ver0 cmanpreconfig_iface_ver0 = {
	.config_readconfig        = cmanpre_readconfig
};

static struct lcr_iface ifaces_ver0[2] = {
	{
		.name		       	= "cmanpreconfig",
		.version	       	= 0,
		.versions_replace      	= 0,
		.versions_replace_count	= 0,
		.dependencies	       	= 0,
		.dependency_count      	= 0,
		.constructor	       	= NULL,
		.destructor	       	= NULL,
		.interfaces	       	= NULL,
	}
};

static struct lcr_comp cmanpre_comp_ver0 = {
	.iface_count				= 1,
	.ifaces					= ifaces_ver0,
};



__attribute__ ((constructor)) static void cmanpre_comp_register(void) {
	lcr_interfaces_set(&ifaces_ver0[0], &cmanpreconfig_iface_ver0);
	lcr_component_register(&cmanpre_comp_ver0);
}

#define LOCALHOST_IPV4 "127.0.0.1"
#define LOCALHOST_IPV6 "::1"

/* Compare two addresses */
static int ipaddr_equal(struct sockaddr_storage *addr1, struct sockaddr_storage *addr2)
{
	int addrlen = 0;
	struct sockaddr *saddr1 = (struct sockaddr *)addr1;
	struct sockaddr *saddr2 = (struct sockaddr *)addr2;

	if (saddr1->sa_family != saddr2->sa_family)
		return 0;

	if (saddr1->sa_family == AF_INET) {
		addrlen = sizeof(struct sockaddr_in);
	}
	if (saddr1->sa_family == AF_INET6) {
		addrlen = sizeof(struct sockaddr_in6);
	}
	assert(addrlen);

	if (memcmp(saddr1, saddr2, addrlen) == 0)
		return 1;
	else
		return 0;

}

/* Build a localhost ip_address */
static int get_localhost(int family, struct sockaddr_storage *localhost)
{
	char *addr_text;
	struct addrinfo *ainfo;
	struct addrinfo ahints;
	int ret;

	if (family == AF_INET) {
		addr_text = LOCALHOST_IPV4;
	} else {
		addr_text = LOCALHOST_IPV6;
	}

	memset(&ahints, 0, sizeof(ahints));
	ahints.ai_socktype = SOCK_DGRAM;
	ahints.ai_protocol = IPPROTO_UDP;
	ahints.ai_family = family;

	/* Lookup the nodename address */
	ret = getaddrinfo(addr_text, NULL, &ahints, &ainfo);
	if (ret)
		return -1;

	memset(localhost, 0, sizeof(struct sockaddr_storage));
	memcpy(localhost, ainfo->ai_addr, ainfo->ai_addrlen);

	freeaddrinfo(ainfo);
	return 0;
}

/* Return the address family of an IP[46] name */
static int address_family(char *addr, struct sockaddr_storage *ssaddr)
{
	struct addrinfo *ainfo;
	struct addrinfo ahints;
	int family;
	int ret;

	memset(&ahints, 0, sizeof(ahints));
	ahints.ai_socktype = SOCK_DGRAM;
	ahints.ai_protocol = IPPROTO_UDP;

	/* Lookup the nodename address */
	ret = getaddrinfo(addr, NULL, &ahints, &ainfo);
	if (ret)
		return -1;

	memset(ssaddr, 0, sizeof(struct sockaddr_storage));
	memcpy(ssaddr, ainfo->ai_addr, ainfo->ai_addrlen);
	family = ainfo->ai_family;

	freeaddrinfo(ainfo);
	return family;
}


static int add_ifaddr(struct objdb_iface_ver0 *objdb, char *mcast, char *ifaddr, int portnum)
{
	unsigned int totem_object_handle;
	unsigned int interface_object_handle;
	struct sockaddr_storage if_addr, localhost, mcast_addr;
	char tmp[132];
	int ret = 0;

	/* Check the families match */
	if (address_family(mcast, &mcast_addr) !=
	    address_family(ifaddr, &if_addr)) {
		sprintf(error_reason, "Node address family does not match multicast address family");
		return -1;
	}

	/* Check it's not bound to localhost, sigh */
	get_localhost(if_addr.ss_family, &localhost);
	if (ipaddr_equal(&localhost, &if_addr)) {
		sprintf(error_reason, "Node address is localhost, please choose a real host address");
		return -1;
	}

	objdb->object_find_reset(OBJECT_PARENT_HANDLE);
	if (objdb->object_find(OBJECT_PARENT_HANDLE,
			       "totem", strlen("totem"), &totem_object_handle)) {

		objdb->object_create(OBJECT_PARENT_HANDLE, &totem_object_handle,
				     "totem", strlen("totem"));
        }

	objdb->object_find_reset(OBJECT_PARENT_HANDLE);
	if (objdb->object_find(OBJECT_PARENT_HANDLE,
			       "totem", strlen("totem"), &totem_object_handle) == 0) {

		if (objdb->object_create(totem_object_handle, &interface_object_handle,
					 "interface", strlen("interface")) == 0) {

			sprintf(tmp, "%d", num_interfaces);
			objdb->object_key_create(interface_object_handle, "ringnumber", strlen("ringnumber"),
							tmp, strlen(tmp)+1);

			objdb->object_key_create(interface_object_handle, "bindnetaddr", strlen("bindnetaddr"),
							ifaddr, strlen(ifaddr)+1);

			objdb->object_key_create(interface_object_handle, "mcastaddr", strlen("mcastaddr"),
							mcast, strlen(mcast)+1);

			sprintf(tmp, "%d", portnum);
			objdb->object_key_create(interface_object_handle, "mcastport", strlen("mcastport"),
							tmp, strlen(tmp)+1);

			num_interfaces++;
		}
	}
	return ret;
}

static uint16_t generate_cluster_id(char *name)
{
	int i;
	int value = 0;

	for (i=0; i<strlen(name); i++) {
		value <<= 1;
		value += name[i];
	}
	sprintf(error_reason, "Generated cluster id for '%s' is %d\n", name, value & 0xFFFF);
	return value & 0xFFFF;
}

static char *default_mcast(char *nodename, uint16_t cluster_id)
{
        struct addrinfo *ainfo;
        struct addrinfo ahints;
	int ret;
	int family;
	static char addr[132];

        memset(&ahints, 0, sizeof(ahints));

        /* Lookup the the nodename address and use it's IP type to
	   default a multicast address */
        ret = getaddrinfo(nodename, NULL, &ahints, &ainfo);
	if (ret) {
		sprintf(error_reason, "Can't determine address family of nodename %s\n", nodename);
		write_cman_pipe("Can't determine address family of nodename");
		return NULL;
	}

	family = ainfo->ai_family;
	freeaddrinfo(ainfo);

	if (family == AF_INET) {
		snprintf(addr, sizeof(addr), "239.192.%d.%d", cluster_id >> 8, cluster_id % 0xFF);
		return addr;
	}
	if (family == AF_INET6) {
		snprintf(addr, sizeof(addr), "ff15::%x", cluster_id);
		return addr;
	}

	return NULL;
}

static int verify_nodename(struct objdb_iface_ver0 *objdb, char *nodename)
{
	char nodename2[MAX_CLUSTER_MEMBER_NAME_LEN+1];
	char nodename3[MAX_CLUSTER_MEMBER_NAME_LEN+1];
	char *str, *dot = NULL;
	struct ifaddrs *ifa, *ifa_list;
	struct sockaddr *sa;
	unsigned int nodes_handle;
	unsigned int parent_handle;
	int error;

	/* nodename is either from commandline or from uname */
	if (nodelist_byname(objdb, cluster_parent_handle, nodename))
		return 0;

	/* If nodename was from uname, try a domain-less version of it */
	strcpy(nodename2, nodename);
	dot = strchr(nodename2, '.');
	if (dot) {
		*dot = '\0';

		if (nodelist_byname(objdb, cluster_parent_handle, nodename2)) {
			strcpy(nodename, nodename2);
			return 0;
		}
	}

	/* If nodename (from uname) is domain-less, try to match against
	   cluster.conf names which may have domainname specified */
	nodes_handle = nodeslist_init(objdb, cluster_parent_handle, &parent_handle);
	do {
		int len;

		if (objdb_get_string(objdb, nodes_handle, "name", &str)) {
			sprintf(error_reason, "Cannot get node name");
			break;
		}

		strcpy(nodename3, str);
		dot = strchr(nodename3, '.');
		if (dot)
			len = dot-nodename3;
		else
			len = strlen(nodename3);

		if (strlen(nodename2) == len &&
		    !strncmp(nodename2, nodename3, len)) {
			strcpy(nodename, str);
			return 0;
		}
		nodes_handle = nodeslist_next(objdb, parent_handle);
	} while (nodes_handle);


	/* The cluster.conf names may not be related to uname at all,
	   they may match a hostname on some network interface.
	   NOTE: This is IPv4 only */
	error = getifaddrs(&ifa_list);
	if (error)
		return -1;

	for (ifa = ifa_list; ifa; ifa = ifa->ifa_next) {
		/* Restore this */
		strcpy(nodename2, nodename);
		sa = ifa->ifa_addr;
		if (!sa || sa->sa_family != AF_INET)
			continue;

		error = getnameinfo(sa, sizeof(*sa), nodename2,
				    sizeof(nodename2), NULL, 0, 0);
		if (error)
			goto out;

		if (nodelist_byname(objdb, cluster_parent_handle, nodename2)) {
			strcpy(nodename, nodename2);
			goto out;
		}

		/* truncate this name and try again */

		dot = strchr(nodename2, '.');
		if (!dot)
			continue;
		*dot = '\0';

		if (nodelist_byname(objdb, cluster_parent_handle, nodename2)) {
			strcpy(nodename, nodename2);
			goto out;
		}

		/* See if it's the IP address that's in cluster.conf */
		error = getnameinfo(sa, sizeof(*sa), nodename2,
				    sizeof(nodename2), NULL, 0, NI_NUMERICHOST);
		if (error)
			goto out;

		if (nodelist_byname(objdb, cluster_parent_handle, nodename2)) {
			strcpy(nodename, nodename2);
			goto out;
		}
	}

	error = -1;
 out:
	freeifaddrs(ifa_list);
	return error;
}

/* Get any environment variable overrides */
static int get_env_overrides()
{
	if (getenv("CMAN_CLUSTER_NAME")) {
		cluster_name = strdup(getenv("CMAN_CLUSTER_NAME"));
	}

	nodename_env = getenv("CMAN_NODENAME");

	expected_votes = 0;
	if (getenv("CMAN_EXPECTEDVOTES")) {
		expected_votes = atoi(getenv("CMAN_EXPECTEDVOTES"));
		if (expected_votes < 1) {
			expected_votes = 0;
		}
	}

	/* optional port */
	if (getenv("CMAN_IP_PORT")) {
		portnum = atoi(getenv("CMAN_IP_PORT"));
	}

	/* optional security key filename */
	if (getenv("CMAN_KEYFILE")) {
		key_filename = strdup(getenv("CMAN_KEYFILE"));
		if (key_filename == NULL) {
			write_cman_pipe("Cannot allocate memory for key filename");
			return -1;
		}
	}

	/* find our own number of votes */
	if (getenv("CMAN_VOTES")) {
		votes = atoi(getenv("CMAN_VOTES"));
	}

	/* nodeid */
	if (getenv("CMAN_NODEID")) {
		nodeid = atoi(getenv("CMAN_NODEID"));
	}

	if (getenv("CMAN_MCAST_ADDR")) {
		mcast_name = getenv("CMAN_MCAST_ADDR");
	}

	if (getenv("CMAN_2NODE")) {
		two_node = 1;
		expected_votes = 1;
		votes = 1;
	}
	return 0;
}


static int get_nodename(struct objdb_iface_ver0 *objdb)
{
	char *nodeid_str;
	unsigned int object_handle;
	unsigned int node_object_handle;
	unsigned int alt_object;
	int error;

	/* our nodename */
	if (nodename_env != NULL) {
		if (strlen(nodename_env) >= sizeof(nodename)) {
			sprintf(error_reason, "Overridden node name %s is too long", nodename);
			write_cman_pipe("Overridden node name is too long");
			error = -1;
			goto out;
		}

		strcpy(nodename, nodename_env);

		if (objdb->object_find(object_handle,
				       nodename, strlen(nodename),
				       &node_object_handle) != 0) {
			sprintf(error_reason, "Overridden node name %s is not in CCS", nodename);
			write_cman_pipe("Overridden node name is not in CCS");
			error = -1;
			goto out;
		}

	} else {
		struct utsname utsname;

		error = uname(&utsname);
		if (error) {
			sprintf(error_reason, "cannot get node name, uname failed");
			write_cman_pipe("Can't determine local node name");
			error = -1;
			goto out;
		}

		if (strlen(utsname.nodename) >= sizeof(nodename)) {
			sprintf(error_reason, "node name from uname is too long");
			write_cman_pipe("Can't determine local node name");
			error = -1;
			goto out;
		}

		strcpy(nodename, utsname.nodename);
	}
	if (verify_nodename(objdb, nodename))
		return -1;


	/* Add <cman nodename> */
	if ( (node_object_handle = nodelist_byname(objdb, cluster_parent_handle, nodename))) {
		if (objdb_get_string(objdb, node_object_handle, "nodeid", &nodeid_str)) {
			sprintf(error_reason, "This node has no nodeid in cluster.conf");
			write_cman_pipe("This node has no nodeid in cluster.conf");
			return -1;
		}
	}

	objdb->object_find_reset(cluster_parent_handle);

	if (objdb->object_find(cluster_parent_handle,
			       "cman", strlen("cman"),
			       &object_handle) == 0) {

		unsigned int mcast_handle;

		if (!mcast_name) {

			objdb->object_find_reset(object_handle);
			if (objdb->object_find(object_handle,
					       "multicast", strlen("multicast"),
					       &mcast_handle) == 0) {

				objdb_get_string(objdb, mcast_handle, "addr", &mcast_name);
			}
		}

		if (!mcast_name) {
			mcast_name = default_mcast(nodename, cluster_id);
		}

		objdb->object_key_create(object_handle, "nodename", strlen("nodename"),
					 nodename, strlen(nodename)+1);
	}

	nodeid = atoi(nodeid_str);
	error = 0;

	/* optional port */
	if (!portnum) {
		objdb_get_int(objdb, object_handle, "port", &portnum);
		if (!portnum)
			portnum = DEFAULT_PORT;
	}

	if (add_ifaddr(objdb, mcast_name, nodename, portnum))
		return -1;

	/* Get all alternative node names */
	num_nodenames = 1;
	objdb->object_find_reset(node_object_handle);
	while (objdb->object_find(node_object_handle,
				  "altname", strlen("altname"),
				  &alt_object) == 0) {
		unsigned int port;
		char *nodename;
		char *mcast;

		if (objdb_get_string(objdb, alt_object, "name", &nodename)) {
			continue;
		}

		objdb_get_int(objdb, alt_object, "port", &port);
		if (!port)
			port = portnum;

		if (objdb_get_string(objdb, alt_object, "mcast", &mcast)) {
			mcast = mcast_name;
		}

		if (add_ifaddr(objdb, mcast, nodename, portnum))
			return -1;

		num_nodenames++;
	}

out:
	return error;
}

/* These are basically cman overrides to the totem config bits */
static void add_cman_overrides(struct objdb_iface_ver0 *objdb)
{
	unsigned int object_handle;
	char tmp[256];

	/* "totem" key already exists, because we have added the interfaces by now */
	objdb->object_find_reset(OBJECT_PARENT_HANDLE);
	if (objdb->object_find(OBJECT_PARENT_HANDLE,
			       "totem", strlen("totem"),
			       &object_handle) == 0)
	{
		char *value;

		objdb->object_key_create(object_handle, "version", strlen("version"),
					 "2", 2);

		sprintf(tmp, "%d", nodeid);
		objdb->object_key_create(object_handle, "nodeid", strlen("nodeid"),
					 tmp, strlen(tmp)+1);

		objdb->object_key_create(object_handle, "vsftype", strlen("vsftype"),
					 "none", strlen("none")+1);

		/* Set the token timeout is 10 seconds, but don't overrride anything that
		   might be in cluster.conf */
		if (objdb_get_string(objdb, object_handle, "token", &value)) {
			objdb->object_key_create(object_handle, "token", strlen("token"),
						 "10000", strlen("10000")+1);
		}
		if (objdb_get_string(objdb, object_handle, "token_retransmits_before_loss_const", &value)) {
			objdb->object_key_create(object_handle, "token_retransmits_before_loss_const",
						 strlen("token_retransmits_before_loss_const"),
						 "20", strlen("20")+1);
		}

		/* Extend consensus & join timeouts per bz#214290 */
		if (objdb_get_string(objdb, object_handle, "join", &value)) {
			objdb->object_key_create(object_handle, "join", strlen("join"),
						 "60", strlen("60")+1);
		}
		if (objdb_get_string(objdb, object_handle, "consensus", &value)) {
			objdb->object_key_create(object_handle, "consensus", strlen("consensus"),
						 "4800", strlen("4800")+1);
		}


		/* Set RRP mode appropriately */
		if (objdb_get_string(objdb, object_handle, "rrp_mode", &value)) {
			if (num_interfaces > 1) {
				objdb->object_key_create(object_handle, "rrp_mode", strlen("rrp_mode"),
							 "active", strlen("active")+1);
			}
			else {
				objdb->object_key_create(object_handle, "rrp_mode", strlen("rrp_mode"),
							 "none", strlen("none")+1);
			}
		}

		if (objdb_get_string(objdb, object_handle, "secauth", &value)) {
			sprintf(tmp, "%d", 1);
			objdb->object_key_create(object_handle, "secauth", strlen("secauth"),
						 tmp, strlen(tmp)+1);
		}

		/* optional security key filename */
		if (!key_filename) {
			objdb_get_string(objdb, object_handle, "keyfile", &key_filename);
		}
		else {
			objdb->object_key_create(object_handle, "keyfile", strlen("keyfile"),
						 key_filename, strlen(key_filename)+1);
		}
		if (!key_filename) {
			/* Use the cluster name as key,
			 * This isn't a good isolation strategy but it does make sure that
			 * clusters on the same port/multicast by mistake don't actually interfere
			 * and that we have some form of encryption going.
			 */

			int keylen;
			memset(tmp, 0, sizeof(tmp));

			strcpy(tmp, cluster_name);

			/* Key length must be a multiple of 4 */
			keylen = (strlen(cluster_name)+4) & 0xFC;
			objdb->object_key_create(object_handle, "key", strlen("key"),
						 tmp, keylen);
		}
	}

	/* Make sure mainconfig doesn't stomp on our logging options */
	objdb->object_find_reset(OBJECT_PARENT_HANDLE);
	if (objdb->object_find(OBJECT_PARENT_HANDLE,
			       "logging", strlen("logging"), &object_handle)) {

                objdb->object_create(OBJECT_PARENT_HANDLE, &object_handle,
					    "logging", strlen("logging"));
        }

	objdb->object_find_reset(OBJECT_PARENT_HANDLE);
	if (objdb->object_find(OBJECT_PARENT_HANDLE,
			       "logging", strlen("logging"),
			       &object_handle) == 0) {
		unsigned int logger_object_handle;
		char *logstr;
		char *logfacility;


		logfacility = logsys_facility_name_get(SYSLOGFACILITY);

		if (objdb_get_string(objdb, object_handle, "syslog_facility", &logstr)) {
			objdb->object_key_create(object_handle, "syslog_facility", strlen("syslog_facility"),
						 logfacility, strlen(logfacility)+1);
		}

		objdb->object_create(object_handle, &logger_object_handle,
				      "logger_subsys", strlen("logger_subsys"));
		objdb->object_key_create(logger_object_handle, "subsys", strlen("subsys"),
					 "CMAN", strlen("CMAN")+1);

		if (objdb_get_string(objdb, object_handle, "to_file", &logstr)) {
			objdb->object_key_create(object_handle, "to_file", strlen("to_file"),
						 "yes", strlen("yes")+1);
		}

		if (objdb_get_string(objdb, object_handle, "logfile", &logstr)) {
			objdb->object_key_create(object_handle, "logfile", strlen("logfile"),
						 LOGDIR "/cman.log", strlen(LOGDIR "/cman.log")+1);
		}

		if (debug_mask) {
			objdb->object_key_create(logger_object_handle, "debug", strlen("debug"),
						 "on", strlen("on")+1);
			objdb->object_key_create(object_handle, "to_stderr", strlen("to_stderr"),
						 "yes", strlen("yes")+1);
		}
	}

	/* Don't run under user "ais" */
	objdb->object_find_reset(OBJECT_PARENT_HANDLE);
	if (objdb->object_find(OBJECT_PARENT_HANDLE, "aisexec", strlen("aisexec"), &object_handle) == 0)
	{
		objdb->object_key_create(object_handle, "user", strlen("user"),
				 "root", strlen("root") + 1);
		objdb->object_key_create(object_handle, "group", strlen("group"),
				 "root", strlen("root") + 1);
	}

	objdb->object_find_reset(cluster_parent_handle);
	if (objdb->object_find(cluster_parent_handle, "cman", strlen("cman"), &object_handle) == 0)
	{
		char str[255];

		sprintf(str, "%d", cluster_id);

		objdb->object_key_create(object_handle, "cluster_id", strlen("cluster_id"),
					 str, strlen(str) + 1);

		if (two_node) {
			sprintf(str, "%d", 1);
			objdb->object_key_create(object_handle, "two_node", strlen("two_node"),
						 str, strlen(str) + 1);
		}

	}

	/* Make sure we load our alter-ego - the main cman module */
	objdb->object_create(OBJECT_PARENT_HANDLE, &object_handle,
			     "service", strlen("service"));
	objdb->object_key_create(object_handle, "name", strlen("name"),
				 "openais_cman", strlen("openais_cman") + 1);
	objdb->object_key_create(object_handle, "ver", strlen("ver"),
				 "0", 2);
}

/* If ccs is not available then use some defaults */
static int set_noccs_defaults(struct objdb_iface_ver0 *objdb)
{
	char tmp[255];
	unsigned int object_handle;

	/* Enforce key */
	key_filename = NOCCS_KEY_FILENAME;

	if (!cluster_name)
		cluster_name = DEFAULT_CLUSTER_NAME;

	if (!cluster_id)
		cluster_id = generate_cluster_id(cluster_name);

	if (!nodename_env) {
		int error;
		struct utsname utsname;

		error = uname(&utsname);
		if (error) {
			sprintf(error_reason, "cannot get node name, uname failed");
			write_cman_pipe("Can't determine local node name");
			return -1;
		}

		nodename_env = (char *)&utsname.nodename;
	}
	strcpy(nodename, nodename_env);
	num_nodenames = 1;

	if (!mcast_name) {
		mcast_name = default_mcast(nodename, cluster_id);
	}

	/* This will increase as nodes join the cluster */
	if (!expected_votes)
		expected_votes = 1;
	if (!votes)
		votes = 1;

	if (!portnum)
		portnum = DEFAULT_PORT;

	/* Invent a node ID */
	if (!nodeid) {
		struct addrinfo *ainfo;
		struct addrinfo ahints;
		int ret;

		memset(&ahints, 0, sizeof(ahints));
		ret = getaddrinfo(nodename, NULL, &ahints, &ainfo);
		if (ret) {
			sprintf(error_reason, "Can't determine address family of nodename %s\n", nodename);
			write_cman_pipe("Can't determine address family of nodename");
			return -1;
		}

		if (ainfo->ai_family == AF_INET) {
			struct sockaddr_in *addr = (struct sockaddr_in *)ainfo->ai_addr;
			memcpy(&nodeid, &addr->sin_addr, sizeof(int));
		}
		if (ainfo->ai_family == AF_INET6) {
			struct sockaddr_in6 *addr = (struct sockaddr_in6 *)ainfo->ai_addr;
			memcpy(&nodeid, &addr->sin6_addr.s6_addr32[3], sizeof(int));
		}
		freeaddrinfo(ainfo);
	}

	/* Write a local <clusternode> entry to keep the rest of the code happy */
	objdb->object_create(cluster_parent_handle, &object_handle,
			     "clusternodes", strlen("clusternodes"));
	objdb->object_create(object_handle, &object_handle,
			     "clusternode", strlen("clusternode"));
	objdb->object_key_create(object_handle, "name", strlen("name"),
				 nodename, strlen(nodename)+1);

	sprintf(tmp, "%d", votes);
	objdb->object_key_create(object_handle, "votes", strlen("votes"),
				 tmp, strlen(tmp)+1);

	sprintf(tmp, "%d", nodeid);
	objdb->object_key_create(object_handle, "nodeid", strlen("nodeid"),
				 tmp, strlen(tmp)+1);

	/* Write the default cluster name & ID in here too */
	objdb->object_key_create(cluster_parent_handle, "name", strlen("name"),
				 cluster_name, strlen(cluster_name)+1);


        if (objdb->object_find(cluster_parent_handle,
			       "cman", strlen("cman"), &object_handle)) {

                objdb->object_create(cluster_parent_handle, &object_handle,
                                            "cman", strlen("cman"));
        }

	objdb->object_find_reset(cluster_parent_handle);
	if (objdb->object_find(cluster_parent_handle,
			       "cman", strlen("cman"),
			       &object_handle) == 0) {

		sprintf(tmp, "%d", cluster_id);
		objdb->object_key_create(object_handle, "cluster_id", strlen("cluster_id"),
					 tmp, strlen(tmp)+1);

		sprintf(tmp, "%d", expected_votes);
		objdb->object_key_create(object_handle, "expected_votes", strlen("expected_votes"),
					 tmp, strlen(tmp)+1);
	}
	return 0;
}

static int get_cman_globals(struct objdb_iface_ver0 *objdb)
{
	unsigned int object_handle;

	objdb_get_string(objdb, cluster_parent_handle, "name", &cluster_name);

	/* Get the <cman> bits that override <totem> bits */
	objdb->object_find_reset(cluster_parent_handle);
	if (objdb->object_find(cluster_parent_handle,
			       "cman", strlen("cman"),
			       &object_handle) == 0) {
		if (!portnum)
			objdb_get_int(objdb, object_handle, "port", &portnum);

		if (!key_filename)
			objdb_get_string(objdb, object_handle, "keyfile", &key_filename);

		if (!cluster_id)
			objdb_get_int(objdb, object_handle, "cluster_id", &cluster_id);

		if (!cluster_id)
			cluster_id = generate_cluster_id(cluster_name);
	}
	return 0;
}

static int cmanpre_readconfig(struct objdb_iface_ver0 *objdb, char **error_string)
{
	int ret = 0;
	unsigned int object_handle;

	if (getenv("CMAN_PIPE"))
                startup_pipe = atoi(getenv("CMAN_PIPE"));

	/* We need to set this up to internal defaults too early */
	openlog("openais", LOG_CONS|LOG_PID, SYSLOGFACILITY);


	objdb->object_find_reset(OBJECT_PARENT_HANDLE);
        objdb->object_find(OBJECT_PARENT_HANDLE,
			   "cluster", strlen("cluster"), &cluster_parent_handle);

	if (objdb->object_find(cluster_parent_handle,
			       "cman", strlen("cman"), &object_handle)) {

                objdb->object_create(cluster_parent_handle, &object_handle,
				     "cman", strlen("cman"));
        }

	get_env_overrides();
	if (getenv("CMAN_NOCONFIG"))
		ret = set_noccs_defaults(objdb);
	else
		ret = get_cman_globals(objdb);

	if (!ret) {
		ret = get_nodename(objdb);
		add_cman_overrides(objdb);
	}
	if (!ret) {
		sprintf (error_reason, "%s", "Successfully parsed cman config\n");
	}
	else {
		if (error_reason[0] == '\0')
			sprintf (error_reason, "%s", "Error parsing cman config\n");
	}
        *error_string = error_reason;

	return ret;
}
