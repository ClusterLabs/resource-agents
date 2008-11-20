#include <syslog.h>

#include "config.h"
#include "dlm_daemon.h"

#include <glib.h>
#include <bzlib.h>
#include <heartbeat/ha_msg.h>

#include <pacemaker/crm_config.h>

#include <pacemaker/crm/crm.h>
#include <pacemaker/crm/ais.h>
/* heartbeat support is irrelevant here */
#undef SUPPORT_HEARTBEAT 
#define SUPPORT_HEARTBEAT 0
#include <pacemaker/crm/common/cluster.h>

#define COMMS_DIR     "/sys/kernel/config/dlm/cluster/comms"

int setup_ccs(void)
{
    /* To avoid creating an additional place for the dlm to be configured,
     * only allow configuration from the command-line until CoroSync is stable
     * enough to be used with Pacemaker
     */
    cfgd_groupd_compat = 0; /* always use libcpg and disable backward compatability */
    return 0;
}

void close_ccs(void) { return; }
int get_weight(int nodeid, char *lockspace) { return 1; }

/* TODO: Make this configurable
 * Can't use logging.c as-is as whitetank exposes a different logging API
 */
void init_logging(void) {
    openlog("cluster-dlm", LOG_PERROR|LOG_PID|LOG_CONS|LOG_NDELAY, LOG_DAEMON);
    /* cl_log_enable_stderr(TRUE); */
}

void setup_logging(void) { return; }
void close_logging(void) {
    closelog();
}

extern int ais_fd_async;

int local_node_id = 0;
char *local_node_uname = NULL;
void dlm_process_node(gpointer key, gpointer value, gpointer user_data);

int setup_cluster(void)
{
    int retries = 0;
    int rc = SA_AIS_OK;
    struct utsname name;

    crm_peer_init();

    if(local_node_uname == NULL) {
	if(uname(&name) < 0) {
	    cl_perror("uname(2) call failed");
	    exit(100);
	}
	local_node_uname = crm_strdup(name.nodename);
	log_debug("Local node name: %s", local_node_uname);
    }
    
    /* 16 := CRM_SERVICE */
  retry:
    log_debug("Creating connection to our AIS plugin");
    rc = saServiceConnect (&ais_fd_sync, &ais_fd_async, CRM_SERVICE);
    if (rc != SA_AIS_OK) {
	log_error("Connection to our AIS plugin (%d) failed: %s (%d)", CRM_SERVICE, ais_error2text(rc), rc);
    }

    switch(rc) {
	case SA_AIS_OK:
	    break;
	case SA_AIS_ERR_TRY_AGAIN:
	    if(retries < 30) {
		sleep(1);
		retries++;
		goto retry;
	    }
	    log_error("Retry count exceeded");
	    return 0;
	default:
	    return 0;
    }

    log_debug("AIS connection established");

    {
	int pid = getpid();
	char *pid_s = crm_itoa(pid);
	send_ais_text(0, pid_s, TRUE, NULL, crm_msg_ais);
	crm_free(pid_s);
    }

    /* Sign up for membership updates */
    send_ais_text(crm_class_notify, "true", TRUE, NULL, crm_msg_ais);
    
    /* Requesting the current list of known nodes */
    send_ais_text(crm_class_members, __FUNCTION__, TRUE, NULL, crm_msg_ais);

    our_nodeid = get_ais_nodeid();
    log_debug("Local node id: %d", our_nodeid);

    return ais_fd_async;
}

static void statechange(void)
{
    static uint64_t last_membership = 0;
    cluster_quorate = crm_have_quorum;
    if(last_membership < crm_peer_seq) {
	log_debug("Processing membership %llu", crm_peer_seq);
	g_hash_table_foreach(crm_peer_cache, dlm_process_node, &last_membership);
	last_membership = crm_peer_seq;
    }
}

void update_cluster(void)
{
    statechange();
}

void process_cluster(int ci)
{
/* ci ::= client number */    
    char *data = NULL;
    char *uncompressed = NULL;

    AIS_Message *msg = NULL;
    SaAisErrorT rc = SA_AIS_OK;
    mar_res_header_t *header = NULL;
    static int header_len = sizeof(mar_res_header_t);

    header = malloc(header_len);
    memset(header, 0, header_len);
    
    errno = 0;
    rc = saRecvRetry(ais_fd_async, header, header_len);
    if (rc != SA_AIS_OK) {
	cl_perror("Receiving message header failed: (%d) %s", rc, ais_error2text(rc));
	goto bail;

    } else if(header->size == header_len) {
	log_error("Empty message: id=%d, size=%d, error=%d, header_len=%d",
		  header->id, header->size, header->error, header_len);
	goto done;
	
    } else if(header->size == 0 || header->size < header_len) {
	log_error("Mangled header: size=%d, header=%d, error=%d",
		  header->size, header_len, header->error);
	goto done;
	
    } else if(header->error != 0) {
	log_error("Header contined error: %d", header->error);
    }
    
    header = realloc(header, header->size);
    /* Use a char* so we can store the remainder into an offset */
    data = (char*)header;

    errno = 0;
    rc = saRecvRetry(ais_fd_async, data+header_len, header->size - header_len);
    msg = (AIS_Message*)data;

    if (rc != SA_AIS_OK) {
	cl_perror("Receiving message body failed: (%d) %s", rc, ais_error2text(rc));
	goto bail;
    }
    
    data = msg->data;
    if(msg->is_compressed && msg->size > 0) {
	int rc = BZ_OK;
	unsigned int new_size = msg->size;

	if(check_message_sanity(msg, NULL) == FALSE) {
	    goto badmsg;
	}

	log_debug("Decompressing message data");
	uncompressed = malloc(new_size);
	memset(uncompressed, 0, new_size);
	
	rc = BZ2_bzBuffToBuffDecompress(
	    uncompressed, &new_size, data, msg->compressed_size, 1, 0);

	if(rc != BZ_OK) {
	    log_error("Decompression failed: %d", rc);
	    goto badmsg;
	}
	
	CRM_ASSERT(rc == BZ_OK);
	CRM_ASSERT(new_size == msg->size);

	data = uncompressed;

    } else if(check_message_sanity(msg, data) == FALSE) {
	goto badmsg;

    } else if(safe_str_eq("identify", data)) {
	int pid = getpid();
	char *pid_s = crm_itoa(pid);
	send_ais_text(0, pid_s, TRUE, NULL, crm_msg_ais);
	crm_free(pid_s);
	goto done;
    }

    if(msg->header.id == crm_class_members) {
	xmlNode *xml = string2xml(data);

	if(xml != NULL) {
	    const char *value = crm_element_value(xml, "id");
	    if(value) {
		crm_peer_seq = crm_int_helper(value, NULL);
	    }

	    log_debug("Updating membership %llu", crm_peer_seq);
	    /* crm_log_xml_info(xml, __PRETTY_FUNCTION__); */
	    xml_child_iter(xml, node, crm_update_ais_node(node, crm_peer_seq));
	    crm_calculate_quorum();
	    statechange();
	    free_xml(xml);
	    
	} else {
	    log_error("Invalid peer update: %s", data);
	}

    } else {
	log_error("Unexpected AIS message type: %d", msg->header.id);
    }

  done:
    free(uncompressed);
    free(msg);
    return;

  badmsg:
    log_error("Invalid message (id=%d, dest=%s:%s, from=%s:%s.%d):"
	      " min=%d, total=%d, size=%d, bz2_size=%d",
	      msg->id, ais_dest(&(msg->host)), msg_type2text(msg->host.type),
	      ais_dest(&(msg->sender)), msg_type2text(msg->sender.type),
	      msg->sender.pid, (int)sizeof(AIS_Message),
	      msg->header.size, msg->size, msg->compressed_size);
    goto done;
    
  bail:
    log_error("AIS connection failed");
    return;
}

void close_cluster(void) {
    /* TODO: Implement something for this */
    return;
}

#include <arpa/inet.h>
#include <openais/totem/totemip.h>

void dlm_process_node(gpointer key, gpointer value, gpointer user_data)
{
    int rc = 0;
    struct stat tmp;
    char path[PATH_MAX];
    crm_node_t *node = value;
    uint64_t *last = user_data;
    const char *action = "Skipped";

    gboolean do_add = FALSE;
    gboolean do_remove = FALSE;
    gboolean is_active = FALSE;

    memset(path, 0, PATH_MAX);
    snprintf(path, PATH_MAX, "%s/%d", COMMS_DIR, node->id);

    rc = stat(path, &tmp);
    is_active = crm_is_member_active(node);
    
    if(rc == 0 && is_active) {
	/* nothing to do?
	 * maybe the node left and came back...
	 */
    } else if(rc == 0) {
	do_remove = TRUE;

    } else if(is_active) {
	do_add = TRUE;
    }

    if(do_remove) {
	action = "Removed";
	del_configfs_node(node->id);
    }

    if(do_add) {
	char *addr_copy = strdup(node->addr);
	char *addr_top = addr_copy;
	char *addr = NULL;
	
	if(do_remove) {
	    action = "Re-added";
	} else {
	    action = "Added";
	}
	
	if(local_node_id == 0) {
	    crm_node_t *local_node = g_hash_table_lookup(
		crm_peer_cache, local_node_uname);
	    local_node_id = local_node->id;
	}
	
	do {
	    char ipaddr[1024];
	    int addr_family = AF_INET;
	    int cna_len = 0, rc = 0;
	    struct sockaddr_storage cna_addr;
	    struct totem_ip_address totem_addr;
	    
	    addr = strsep(&addr_copy, " ");
	    if(addr == NULL) {
		break;
	    }
	    
	    /* do_cmd_get_node_addrs */
	    if(strstr(addr, "ip(") == NULL) {
		continue;
		
	    } else if(strchr(addr, ':')) {
		rc = sscanf(addr, "ip(%[0-9A-Fa-f:])", ipaddr);
		if(rc != 1) {
		    log_error("Could not extract IPv6 address from '%s'", addr);
		    continue;			
		}
		addr_family = AF_INET6;
		    
	    } else {
		rc = sscanf(addr, "ip(%[0-9.]) ", ipaddr);
		if(rc != 1) {
		    log_error("Could not extract IPv4 address from '%s'", addr);
		    continue;			
		}
	    }
		
	    rc = inet_pton(addr_family, ipaddr, &totem_addr);
	    if(rc != 1) {
		log_error("Could not parse '%s' as in IPv%c address", ipaddr, (addr_family==AF_INET)?'4':'6');
		continue;
	    }

	    rc = totemip_parse(&totem_addr, ipaddr, addr_family);
	    if(rc != 0) {
		log_error("Could not convert '%s' into a totem address", ipaddr);
		continue;
	    }

	    rc = totemip_totemip_to_sockaddr_convert(&totem_addr, 0, &cna_addr, &cna_len);
	    if(rc != 0) {
		log_error("Could not convert totem address for '%s' into sockaddr", ipaddr);
		continue;
	    }

	    log_debug("Adding address %s to configfs for node %u/%s ", addr, node->id, node->uname);
	    add_configfs_node(node->id, ((char*)&cna_addr), cna_len, (node->id == local_node_id));

	} while(addr != NULL);
	free(addr_top);
    }

    log_debug("%s %sctive node %u '%s': born-on=%llu, last-seen=%llu, this-event=%llu, last-event=%llu",
	      action, crm_is_member_active(value)?"a":"ina",
	      node->id, node->uname, node->born, node->last_seen,
	      crm_peer_seq, (unsigned long long)*last);
}

int is_cluster_member(int nodeid)
{
    crm_node_t *node = crm_get_peer(nodeid, NULL);
    return crm_is_member_active(node);
}

char *nodeid2name(int nodeid) {
    crm_node_t *node = crm_get_peer(nodeid, NULL);
    if(node->uname == NULL) {
	return NULL;
    }
    return strdup(node->uname);
}

void kick_node_from_cluster(int nodeid)
{
    log_error("%s not yet implemented", __FUNCTION__);
    return;
}
