#include <sys/poll.h>
#include <sys/types.h>
#include <sys/errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <unistd.h>
#include <string.h>
#include <syslog.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/utsname.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>

/* openais headers */
#include <openais/service/objdb.h>
#include <openais/service/swab.h>
#include <openais/totem/totemip.h>
#include <openais/totem/totempg.h>
#include <openais/totem/aispoll.h>
#include <openais/service/service.h>
#include <openais/service/config.h>
#include <openais/lcr/lcr_comp.h>
#include <openais/service/swab.h>
#include <openais/service/logsys.h>

#include "list.h"
#include "cnxman-socket.h"
#include "cnxman-private.h"
#include "commands.h"
#include "logging.h"

#include "ais.h"
#include "cman.h"
#include "cmanconfig.h"
#include "daemon.h"

extern int our_nodeid();
extern char cluster_name[MAX_CLUSTER_NAME_LEN+1];
extern char *key_filename;
extern unsigned int quorumdev_poll;
extern unsigned int ccsd_poll_interval;
extern unsigned int shutdown_timeout;
extern int init_config(struct objdb_iface_ver0 *objdb);

struct totem_ip_address mcast_addr[MAX_INTERFACES];
struct totem_ip_address ifaddrs[MAX_INTERFACES];
int num_interfaces;
uint64_t incarnation;
int num_ais_nodes;
extern unsigned int config_version;
static unsigned int cluster_parent_handle;

static int startup_pipe;
static unsigned int debug_mask;
static int first_trans = 1;
static totempg_groups_handle group_handle;
static struct totempg_group cman_group[1] = {
        { .group          = "CMAN", .group_len      = 4},
};

LOGSYS_DECLARE_SUBSYS (CMAN_NAME, LOG_INFO);

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

static void cman_deliver_fn(unsigned int nodeid, struct iovec *iovec, int iov_len,
			    int endian_conversion_required);
static void cman_confchg_fn(enum totem_configuration_type configuration_type,
			    unsigned int *member_list, int member_list_entries,
			    unsigned int *left_list, int left_list_entries,
			    unsigned int *joined_list, int joined_list_entries,
			    struct memb_ring_id *ring_id);

/* Plugin-specific code */
/* Need some better way of determining these.... */
#define CMAN_SERVICE 9

static int cman_exit_fn(void *conn_info);
static int cman_exec_init_fn(struct objdb_iface_ver0 *objdb);

/*
 * Exports the interface for the service
 */
static struct openais_service_handler cman_service_handler = {
	.name		    		= (char *)"openais CMAN membership service 2.90",
	.id			        = CMAN_SERVICE,
	.flow_control			= OPENAIS_FLOW_CONTROL_NOT_REQUIRED,
	.lib_exit_fn		       	= cman_exit_fn,
	.exec_init_fn		       	= cman_exec_init_fn,
	.config_init_fn                 = NULL,
};

static struct openais_service_handler *cman_get_handler_ver0(void)
{
	return (&cman_service_handler);
}

static struct openais_service_handler_iface_ver0 cman_service_handler_iface = {
	.openais_get_service_handler_ver0 = cman_get_handler_ver0
};


static struct lcr_iface ifaces_ver0[1] = {
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
	.iface_count				= 1,
	.ifaces					= ifaces_ver0,
};



__attribute__ ((constructor)) static void cman_comp_register(void) {
	lcr_interfaces_set(&ifaces_ver0[0], &cman_service_handler_iface);
	lcr_component_register(&cman_comp_ver0);
}

/* ------------------------------- */


static int cman_exec_init_fn(struct objdb_iface_ver0 *objdb)
{
	unsigned int object_handle;
	char pipe_msg[256];

	if (getenv("CMAN_DEBUGLOG"))
		debug_mask = atoi(getenv("CMAN_DEBUGLOG"));

	set_debuglog(debug_mask);

	/* We need to set this up to internal defaults too early */
	openlog("openais", LOG_CONS|LOG_PID, SYSLOGFACILITY);

	/* Enable stderr logging if requested by cman_tool */
	if (debug_mask) {
		logsys_config_subsys_set("CMAN", LOGSYS_TAG_LOG, LOG_DEBUG);
	}

	if (getenv("CMAN_PIPE"))
                startup_pipe = atoi(getenv("CMAN_PIPE"));

	P_DAEMON("CMAN starting");

        /* Get our config variables */
	objdb->object_find_reset(OBJECT_PARENT_HANDLE);
	objdb->object_find(OBJECT_PARENT_HANDLE,
		"cluster", strlen("cluster"), &cluster_parent_handle);

	if (objdb->object_find(cluster_parent_handle, "cman", strlen("cman"), &object_handle) == 0)
	{
		objdb_get_int(objdb, object_handle, "quorum_dev_poll", &quorumdev_poll);
		objdb_get_int(objdb, object_handle, "shutdown_timeout", &shutdown_timeout);
		objdb_get_int(objdb, object_handle, "ccsd_poll", &ccsd_poll_interval);

		/* Only use the CCS version of this if it was not overridden on the command-line */
		if (!getenv("CMAN_DEBUGLOG"))
		{
			objdb_get_int(objdb, object_handle, "debug_mask", &debug_mask);
			set_debuglog(debug_mask);
		}
	}

	/* Open local sockets and initialise I/O queues */
	read_cman_config(objdb, &config_version);
	cman_init();

	/* Let cman_tool know we are running and our PID */
	sprintf(pipe_msg,"SUCCESS: %d", getpid());
	write_cman_pipe(pipe_msg);
	close(startup_pipe);
	startup_pipe = 0;

	/* Start totem */
	totempg_groups_initialize(&group_handle, cman_deliver_fn, cman_confchg_fn);
	totempg_groups_join(group_handle, cman_group, 1);

	return 0;
}


int cman_exit_fn(void *conn_info)
{
	cman_finish();
	return 0;
}

/* END Plugin-specific code */

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

/* This assumes the iovec has only one element ... is it true ?? */
static void cman_deliver_fn(unsigned int nodeid, struct iovec *iovec, int iov_len,
			    int endian_conversion_required)
{
	struct cl_protheader *header = iovec->iov_base;
	char *buf = iovec->iov_base;

	P_AIS("deliver_fn called, iov_len = %d, iov[0].len = %lu, source nodeid = %d, conversion reqd=%d\n",
	      iov_len, (unsigned long)iovec->iov_len, nodeid, endian_conversion_required);

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
				 buf + sizeof(struct cl_protheader),
				 iovec->iov_len - sizeof(struct cl_protheader),
				 endian_conversion_required);
	}
}

static void cman_confchg_fn(enum totem_configuration_type configuration_type,
			    unsigned int *member_list, int member_list_entries,
			    unsigned int *left_list, int left_list_entries,
			    unsigned int *joined_list, int joined_list_entries,
			    struct memb_ring_id *ring_id)
{
	int i;
	static int last_memb_count = 0;
	static int saved_left_list_entries;
	static int saved_left_list_size;
	static unsigned int *saved_left_list = NULL;

	P_AIS("confchg_fn called type = %d, seq=%lld\n", configuration_type, ring_id->seq);

	incarnation = ring_id->seq;
	num_ais_nodes = member_list_entries;

	/* Tell the cman membership layer */
	for (i=0; i<left_list_entries; i++)
		del_ais_node(left_list[i]);

	/* Joining nodes are only added after a valid TRANSITION message
	 * is received.
	 */

	/* Save the left list for later so we can do a consolidated confchg message */
	if (configuration_type == TOTEM_CONFIGURATION_TRANSITIONAL) {
		if (saved_left_list == NULL) {
			saved_left_list_size = left_list_entries*2;
			saved_left_list = malloc(sizeof(int) * saved_left_list_size);
			if (!saved_left_list) {
				log_printf(LOG_LEVEL_CRIT, "cannot allocate memory for confchg message");
				exit(3);
			}
		}
		if (saved_left_list_size < left_list_entries) {
			saved_left_list_size = left_list_entries*2;
			saved_left_list = realloc(saved_left_list, sizeof(int) * saved_left_list_size);
			if (!saved_left_list) {
				log_printf(LOG_LEVEL_CRIT, "cannot reallocate memory for confchg message");
				exit(3);
			}
		}
		saved_left_list_entries = left_list_entries;
		memcpy(saved_left_list, left_list, left_list_entries * sizeof(int));
	}

	if (configuration_type == TOTEM_CONFIGURATION_REGULAR) {
		P_AIS("last memb_count = %d, current = %d\n", last_memb_count, member_list_entries);
		send_transition_msg(last_memb_count, first_trans);
		last_memb_count = member_list_entries;
		first_trans = 0;

		cman_send_confchg(member_list,  member_list_entries,
				  saved_left_list, saved_left_list_entries,
				  joined_list, joined_list_entries);
	}
}

/* Write an error message down the CMAN startup pipe so
   that cman_tool can display it */
int write_cman_pipe(char *message)
{
	if (startup_pipe)
		return write(startup_pipe, message, strlen(message)+1);

	return 0;
}
