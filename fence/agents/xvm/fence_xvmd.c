/*
 * Author: Lon Hohberger <lhh at redhat.com>
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/un.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <sys/ioctl.h>
#include <arpa/inet.h>
#include <net/if.h>
#include <netinet/in.h>
#include <netdb.h>
#include <sys/time.h>
#include <fcntl.h>
#include <errno.h>
#include <pthread.h>
#include <virterror.h>
#include <nss.h>
#include <libgen.h>
#include <ccs.h>

/* Local includes */
#include "xvm.h"
#include "simple_auth.h"
#include "options.h"
#include "mcast.h"
#include "tcp.h"
#include "virt.h"
#include "libcman.h"
#include "debug.h"

static int running = 1;
static int reload_key;

LOGSYS_DECLARE_SYSTEM (NULL,
        LOG_MODE_OUTPUT_STDERR |
	LOG_MODE_OUTPUT_SYSLOG_THREADED |
	LOG_MODE_SHORT_FILELINE |
	LOG_MODE_OUTPUT_FILE |
	LOG_MODE_BUFFER_BEFORE_CONFIG,
	LOGDIR "/fence_xvmd.log",
	SYSLOGFACILITY);

LOGSYS_DECLARE_SUBSYS ("XVM", SYSLOGLEVEL);

int cleanup_xml(char *xmldesc, char **ret, size_t *retsz);

int
connect_tcp(fence_req_t *req, fence_auth_type_t auth,
	    void *key, size_t key_len)
{
	int fd = -1;
	struct sockaddr_in sin;
	struct sockaddr_in6 sin6;
	char buf[128];

	switch(req->family) {
	case PF_INET:
		memset(&sin, 0, sizeof(sin));
		memcpy(&sin.sin_addr, req->address,
		       sizeof(sin.sin_addr));
		sin.sin_family = PF_INET;
		fd = ipv4_connect(&sin.sin_addr, req->port,
				  5);
		if (fd < 0) {
			log_printf(LOG_ERR,
				   "Failed to connect to caller: %s\n",
				   strerror(errno));
			return -1;
		}
		break;
	case PF_INET6:
		memset(&sin6, 0, sizeof(sin));
		memcpy(&sin6.sin6_addr, req->address,
		       sizeof(sin6.sin6_addr));
		sin.sin_family = PF_INET6;
		fd = ipv6_connect(&sin6.sin6_addr, req->port,
				  5);

		memset(buf,0,sizeof(buf));
		inet_ntop(PF_INET6, &sin6.sin6_addr, buf, sizeof(buf));

		if (fd < 0) {
			log_printf(LOG_ERR, "Failed to call back %s\n", buf);
			return -1;
		}
		break;
	default:
		dbg_printf(1, "Family = %d\n", req->family);
		return -1;
	}

	/* Noops if auth == AUTH_NONE */
	if (tcp_response(fd, auth, key, key_len, 10) <= 0) {
		log_printf(LOG_ERR, "Failed to respond to challenge\n");
		close(fd);
		return -1;
	}

	if (tcp_challenge(fd, auth, key, key_len, 10) <= 0) {
		log_printf(LOG_ERR, "Remote failed challenge\n");
		close(fd);
		return -1;
	}
	return fd;
}


int
do_notify_caller_tcp(fence_req_t *req, fence_auth_type_t auth,
		     void *key, size_t key_len, char response)
{
	int fd;

	fd = connect_tcp(req, auth, key, key_len);
	if (fd < 0)
		goto out;

	if (write(fd, &response, 1) < 0) {
		perror("write");
	}
out:
	if (fd != -1) {
		close(fd);
		return 0;
	}

	return -1;
}


void
raise_error(virConnectPtr vp)
{
	virErrorPtr vep;

	vep = virConnGetLastError(vp);
	if (!vep) {
		log_printf(LOG_ERR,
		   "Error: Unable to retrieve error from connection!\n");
		return;
	}

	log_printf(LOG_ERR, "Error: libvirt #%d domain %d: %s\n", vep->code,
	       vep->domain, vep->message);
}


static inline virDomainPtr
get_domain(fence_req_t *req, virConnectPtr vp)
{
	if (req->flags & RF_UUID) {
		return virDomainLookupByUUIDString(vp,
					(const char *)req->domain);
	}

	return virDomainLookupByName(vp, (const char *)req->domain);
}


static inline int
wait_domain(fence_req_t *req, virConnectPtr vp, int timeout)
{
	int tries = 0;
	int response = 1;
	virDomainPtr vdp;
	virDomainInfo di;

	if (!(vdp = get_domain(req, vp)))
		return 0;

	/* Check domain liveliness.  If the domain is still here,
	   we return failure, and the client must then retry */
	/* XXX On the xen 3.0.4 API, we will be able to guarantee
	   synchronous virDomainDestroy, so this check will not
	   be necessary */
	do {
		sleep(1);
		vdp = get_domain(req, vp);
		if (!vdp) {
			dbg_printf(2, "Domain no longer exists\n");
			response = 0;
			break;
		}

		memset(&di, 0, sizeof(di));
		virDomainGetInfo(vdp, &di);
		virDomainFree(vdp);

		if (di.state == VIR_DOMAIN_SHUTOFF) {
			dbg_printf(2, "Domain has been shut off\n");
			response = 0;
			break;
		}
		
		dbg_printf(4, "Domain still exists (state %d) after %d seconds\n",
			di.state, tries);

		if (++tries >= timeout)
			break;
	} while (1);

	return response;
}




int
do_fence_request_tcp(fence_req_t *req, fence_auth_type_t auth,
		     void *key, size_t key_len, virConnectPtr vp,
		     int flags)
{
	int fd = -1, ret = -1;
	virDomainPtr vdp;
	virDomainInfo vdi;
	char response = 1;
	char *domain_desc, *domain_desc_sanitized;
	size_t sz;

	if (!(vdp = get_domain(req, vp)) && (!(flags & F_NOCLUSTER))) {
		dbg_printf(2, "Could not find domain: %s\n", req->domain);
		goto out;
	}

	fd = connect_tcp(req, auth, key, key_len);
	if (fd < 0) {
		dbg_printf(2, "Could call back for fence request: %s\n", 
			strerror(errno));
		goto out;
	}

	switch(req->request) {
	case FENCE_NULL:
		dbg_printf(1, "NULL operation: returning failure\n");
		response = 1;
		break;
	case FENCE_OFF:
		log_printf(LOG_NOTICE, "Destroying domain %s...\n",
			   (char *)req->domain);
		if (flags & F_NOCLUSTER) {
			if (!vdp ||
			    ((virDomainGetInfo(vdp, &vdi) == 0) &&
			     (vdi.state == VIR_DOMAIN_SHUTOFF))) {
				dbg_printf(2, "[NOCLUSTER] Nothing to "
					   "do - domain does not exist\n");
				response = 0;
				break;
			}
		}


		dbg_printf(2, "[OFF] Calling virDomainDestroy\n");
		ret = virDomainDestroy(vdp);
		if (ret < 0) {
			log_printf(LOG_ERR,
				   "virDomainDestroy() failed: %d\n",
				   ret);
			break;
		}

		response = wait_domain(req, vp, 15);

		if (response) {
			log_printf(LOG_ERR,
			   "Domain %s still exists; fencing failed\n",
			   (char *)req->domain);
		}
		break;
	case FENCE_REBOOT:
		log_printf(LOG_NOTICE, "Rebooting domain %s...\n",
			   (char *)req->domain);

		if (flags & F_NOCLUSTER) {
			if (!vdp ||
			    ((virDomainGetInfo(vdp, &vdi) == 0) &&
			     (vdi.state == VIR_DOMAIN_SHUTOFF))) {
				dbg_printf(2, "[NOCLUSTER] Nothing to "
					   "do - domain does not exist\n");
				response = 0;
				break;
			}
		}

		domain_desc = virDomainGetXMLDesc(vdp, 0);

		if (domain_desc) {
			dbg_printf(3, "[[ XML Domain Info ]]\n");
			dbg_printf(3, "%s\n[[ XML END ]]\n", domain_desc);

			sz = 0;
			if (cleanup_xml(domain_desc,
					&domain_desc_sanitized, &sz) == 0) {
				free(domain_desc);
				domain_desc = domain_desc_sanitized;
			}

			dbg_printf(3, "[[ XML Domain Info (modified) ]]\n");
			dbg_printf(3, "%s\n[[ XML END ]]\n", domain_desc);
		} else {
			dbg_printf(1, "Failed getting domain description from "
				   "libvirt\n");
		}

		dbg_printf(2, "[REBOOT] Calling virDomainDestroy(%p)\n", vdp);
		ret = virDomainDestroy(vdp);
		if (ret < 0) {
			log_printf(LOG_ERR,
				   "virDomainDestroy() failed: %d/%d\n",
				   ret, errno);
			if (domain_desc)
				free(domain_desc);
			break;
		}

		response = wait_domain(req, vp, 15);

		if (response) {
			log_printf(LOG_ERR,
				   "Domain %s still exists; fencing failed\n",
				   (char *)req->domain);
		} else if (domain_desc) {
			/* Recreate the domain if possible */
			/* Success */
			dbg_printf(2, "Calling virDomainCreateLinux()...\n");
			virDomainCreateLinux(vp, domain_desc, 0);
			free(domain_desc);
		}
		break;
	}

	dbg_printf(3, "Sending response to caller...\n");
	if (write(fd, &response, 1) < 0) {
		log_printf(LOG_ERR, "Failed to send response to caller: %s\n",
			   strerror(errno));
	}
out:
	if (fd != -1)
		close(fd);

	return 1;
}


int
virt_list_update(virConnectPtr vp, virt_list_t **vl, int my_id)
{
	virt_list_t *list = NULL;

	list = vl_get(vp, my_id);
	if (!list)
		return -1;

	if (*vl)
		vl_free(*vl);
	*vl = list;

	return 0;
}


int
get_cman_ids(cman_handle_t ch, int *my_id, int *high_id)
{
	int max_nodes;
	int actual;
	cman_node_t *nodes = NULL;
	cman_node_t me;
	int high = 0, ret = -1, x, _local = 0;

	if (!my_id && !high_id)
		return 0;

	if (!ch) {
		_local = 1;
		ch = cman_init(NULL);
	}
	if (!ch)
		return -1;

	max_nodes = cman_get_node_count(ch);
	if (max_nodes <= 0)
		goto out;

	if (my_id) {
		memset(&me, 0, sizeof(me));
		if (cman_get_node(ch, CMAN_NODEID_US, &me) < 0)
			goto out;
		*my_id = me.cn_nodeid;
	}

	if (!high_id) {
		ret = 0;
		goto out;
	}

	nodes = malloc(sizeof(cman_node_t) * max_nodes);
	if (!nodes)
		goto out;
	memset(nodes, 0, sizeof(cman_node_t) * max_nodes);

	if (cman_get_nodes(ch, max_nodes, &actual, nodes) < 0)
		goto out;

	for (x = 0; x < actual; x++)
		if (nodes[x].cn_nodeid > high && nodes[x].cn_member)
			high = nodes[x].cn_nodeid;

	*high_id = high;

	ret = 0;
out:
	if (nodes)
		free(nodes);
	if (ch && _local)
		cman_finish(ch);
	return ret;
}


int
get_domain_state_ckpt(void *hp, unsigned char *domain, vm_state_t *state)
{
	errno = EINVAL;

	if (!hp || !domain || !state || !strlen((char *)domain))
		return -1;
	if (!strcmp(DOMAIN0NAME, (char *)domain))
		return -1;

	return ckpt_read(hp, (char *)domain, state, sizeof(*state));
}


void
store_domains_by_name(void *hp, virt_list_t *vl)
{
	int x;

	if (!vl)
		return;

	for (x = 0; x < vl->vm_count; x++) {
		if (!strcmp(DOMAIN0NAME, vl->vm_states[x].v_name))
			continue;
		dbg_printf(2, "Storing %s\n", vl->vm_states[x].v_name);
		ckpt_write(hp, vl->vm_states[x].v_name, 
			   &vl->vm_states[x].v_state,
			   sizeof(vm_state_t));
	}
}


void
store_domains_by_uuid(void *hp, virt_list_t *vl)
{
	int x;

	if (!vl)
		return;

	for (x = 0; x < vl->vm_count; x++) {
		if (!strcmp(DOMAIN0UUID, vl->vm_states[x].v_uuid))
			continue;
		dbg_printf(2, "Storing %s\n", vl->vm_states[x].v_uuid);
		ckpt_write(hp, vl->vm_states[x].v_uuid, 
			   &vl->vm_states[x].v_state,
			   sizeof(vm_state_t));
	}
}


static void
handle_remote_domain(cman_handle_t ch, void *h, fence_req_t *data,
		     fence_auth_type_t auth, void *key, size_t key_len,
		     int my_id)
{
	vm_state_t vst;
	int high_id;
	int fenced;
	uint64_t fence_time;
	char ret = 1;
	cman_node_t node;
	

	if (get_domain_state_ckpt(h, data->domain, &vst) < 0) {
		dbg_printf(1, "Evaluating Domain: %s   Last Owner/State Unknown\n",
		       data->domain);
		memset(&vst, 0, sizeof(vst));
	} else {
		dbg_printf(1, "Evaluating Domain: %s   Last Owner: %d   State %d\n",
		       data->domain, vst.s_owner, vst.s_state);
	}
			
	if (get_cman_ids(ch, NULL, &high_id) < 0) {
		log_printf(LOG_ERR,
			   "Error: Could not determine high node ID; "
			   "unable to process fencing request\n");
		return;
	}
	
	if (my_id == high_id && vst.s_owner == 0) {
		dbg_printf(1, "There is no record of that domain; "
		       "returning success\n");
		ret = 0;
	} else if (my_id == high_id && vst.s_owner != my_id) {

		memset(&node, 0, sizeof(node));
		cman_get_node(ch, vst.s_owner, &node);
		if (node.cn_nodeid == 0) {
			dbg_printf(1, "Node %d does not exist\n",
				   vst.s_owner);
			return;
		}

		if (node.cn_member) {
			dbg_printf(1,
				   "Node %d is online - not taking action\n",
				   vst.s_owner);
			return;
		}

		fenced = 0;
		cman_get_fenceinfo(ch, vst.s_owner, &fence_time, &fenced, NULL);
		if (fenced == 0) {
			dbg_printf(1, "Node %d is dead but not fenced - not "
				   "taking action\n", vst.s_owner);
			return;
		}

		dbg_printf(1, "Node %d is dead & fenced\n", vst.s_owner);
		ret = 0;
					
	} else if (vst.s_owner == my_id) {
		dbg_printf(1, "I am the last owner of the domain\n");
		ret = 0;
	}

	if (!ret) {
		switch(auth) {
		case AUTH_NONE:
		case AUTH_SHA1:
		case AUTH_SHA256:
		case AUTH_SHA512:
			dbg_printf(1, "Plain TCP request\n");
			do_notify_caller_tcp(data, auth, key, key_len, ret);
			break;
		default:
			dbg_printf(1, "XXX Unhandled authentication\n");
		}
	}
}


int
xvmd_loop(cman_handle_t ch, void *h, int fd, fence_xvm_args_t *args,
	  void *key, size_t key_len)
{
	fd_set rfds;
	struct timeval tv;
	struct sockaddr_in sin;
	int len;
	int n;
	int my_id = 1;
	socklen_t slen;
	fence_req_t data;
	virConnectPtr vp = NULL;
	virt_list_t *vl = NULL;
	virt_state_t *dom = NULL;

	vp = virConnectOpen(args->uri);
  	if (!vp)
		log_printf(LOG_ERR, "virConnectOpen failed: %s",
			   strerror(errno));
  
  	if (!(args->flags & F_NOCLUSTER))
  		get_cman_ids(ch, &my_id, NULL);
  
	dbg_printf(1, "My Node ID = %d\n", my_id);
	
	if (vp) {
		vl = vl_get(vp, my_id);
		vl_print(vl);
		virt_list_update(vp, &vl, my_id);
		if (args->flags & F_USE_UUID) 
			store_domains_by_uuid(h, vl);
		else
			store_domains_by_name(h, vl);
	}

	while (running) {
		FD_ZERO(&rfds);
		FD_SET(fd, &rfds);
		tv.tv_sec = 10;
		tv.tv_usec = 0;

		/* Close the connection */
		if (vp) {
			virConnectClose(vp);
			vp = NULL;
		}

		if (reload_key) {
			char temp_key[MAX_KEY_LEN];
			int ret;

			reload_key = 0;

			ret = read_key_file(args->key_file, temp_key,
					    sizeof(temp_key));
			if (ret < 0) {
				log_printf(LOG_ERR, "Could not read %s; not updating key",
					args->key_file);
			} else {
				memcpy(key, temp_key, MAX_KEY_LEN);
				key_len = (size_t) ret;

				if (args->auth == AUTH_NONE)
					args->auth = AUTH_SHA256;
				if (args->hash == HASH_NONE)
					args->hash = HASH_SHA256;
			}
		}
		
		n = select(fd+1, &rfds, NULL, NULL, &tv);
		if (n < 0)
			continue;
	
		/* Request and/or timeout: open connection */
		vp = virConnectOpen(args->uri);
		if (!vp) {
			log_printf(LOG_NOTICE, "NOTICE: virConnectOpen(): "
				   "%s; cannot fence!\n", strerror(errno));
			continue;
		}
			
		/* Update list of VMs from libvirt. */
		virt_list_update(vp, &vl, my_id);
		vl_print(vl);

		/* Store information here */
		if (!(args->flags & F_NOCLUSTER)) {
			if (args->flags & F_USE_UUID) 
				store_domains_by_uuid(h, vl);
			else
				store_domains_by_name(h, vl);
		}
		
		/* 
		 * If no requests, we're done 
		 */
		if (n == 0)
			continue;

		slen = sizeof(sin);
		len = recvfrom(fd, &data, sizeof(data), 0,
			       (struct sockaddr *)&sin, &slen);
		
		if (len <= 0) {
			log_printf(LOG_ERR, "recvfrom: %s\n",
				   strerror(errno));
			continue;
		}

		if (!verify_request(&data, args->hash, key, key_len)) {
			dbg_printf(1, "Key mismatch; dropping packet\n");
			continue;
		}

		if ((args->flags & F_USE_UUID) &&
		    !(data.flags & RF_UUID)) {
			dbg_printf(1, "Dropping packet: Request to fence by "
			           "name while using UUIDs\n");
			continue;
		}

		if (!(args->flags & F_USE_UUID) &&
		    (data.flags & RF_UUID)) {
			dbg_printf(1, "Dropping packet: Request to fence by "
			           "UUID while using names\n");
			continue;
		}

		dbg_printf(1, "Request to fence: %s\n", data.domain);
		
		if (args->flags & F_USE_UUID)
			dom = vl_find_uuid(vl, (char *)data.domain);
		else
			dom = vl_find_name(vl, (char *)data.domain);
		if (!dom && !(args->flags & F_NOCLUSTER)) {
			handle_remote_domain(ch, h, &data, args->auth,
					     key, key_len, my_id);
			continue;
		}

		dbg_printf(1, "%s is running locally\n", (char *)data.domain);

		switch(args->auth) {
		case AUTH_NONE:
		case AUTH_SHA1:
		case AUTH_SHA256:
		case AUTH_SHA512:
			dbg_printf(1, "Plain TCP request\n");
			do_fence_request_tcp(&data, args->auth, key,
					     key_len, vp, args->flags);
			break;
		default:
			log_printf(LOG_ERR, "XXX Unhandled authentication\n");
		}
	}

	cman_finish(ch);
	
	if (vp) {
		virConnectClose(vp);
		vp = NULL;
	}

	return 0;
}


void
sigint_handler(int sig)
{
	running = 0;
}

void
sighup_handler(int sig)
{
	reload_key = 1;
}

void malloc_dump_table(void);


static void
log_config_done(int logmode)
{
	if(logmode & LOG_MODE_BUFFER_BEFORE_CONFIG) {
		log_printf(LOG_DEBUG, "logsys config enabled from get_logsys_config_data\n");
		logmode &= ~LOG_MODE_BUFFER_BEFORE_CONFIG;
		logmode |= LOG_MODE_FLUSH_AFTER_CONFIG;
		logsys_config_mode_set (logmode);
	}
}


/**
  Grab logsys configuration data from libccs
 */
static int
get_logsys_config_data(int *debug)
{
	int ccsfd = -1, loglevel = SYSLOGLEVEL, facility = SYSLOGFACILITY;
	char *val = NULL, *error = NULL;
	unsigned int logmode = 0;
	int global_debug = 0;

	log_printf(LOG_DEBUG, "Loading logsys configuration information\n");

	ccsfd = ccs_connect();
	if (ccsfd < 0) {
		log_printf(LOG_CRIT, "Connection to CCSD failed; cannot start\n");
		return -1;
	}

	logmode = logsys_config_mode_get();

	if (!debug) {
		if (ccs_get(ccsfd, "/cluster/logging/@debug", &val) == 0) {
			if(!strcmp(val, "on")) {
				global_debug = 1;
			} else 
			if(!strcmp(val, "off")) {
				global_debug = 0;
			} else
				log_printf(LOG_ERR, "global debug: unknown value\n");
			free(val);
			val = NULL;
		}

		if (ccs_get(ccsfd, "/cluster/logging/logger_subsys[@subsys=\"XVM\"]/@debug", &val) == 0) {
			if(!strcmp(val, "on")) {
				*debug = 1;
			} else 
			if(!strcmp(val, "off")) { /* debug from cmdline/envvars override config */
				*debug = 0;
			} else
				log_printf(LOG_ERR, "subsys debug: unknown value: %s\n", val);
			free(val);
			val = NULL;
		} else
			*debug = global_debug; /* global debug overrides subsystem only if latter is not specified */

		if (ccs_get(ccsfd, "/cluster/logging/logger_subsys[@subsys=\"XVM\"]/@syslog_level", &val) == 0) {
			loglevel = logsys_priority_id_get (val);
			if (loglevel < 0)
				loglevel = SYSLOGLEVEL;

			if (!*debug) {
				if (loglevel == LOG_LEVEL_DEBUG)
					*debug = 1;

				logsys_config_priority_set (loglevel);
			}

			free(val);
			val = NULL;
		} else
		if (ccs_get(ccsfd, "/cluster/fence_xvmd/@log_level", &val) == 0) { /* check backward compat options */
			loglevel = logsys_priority_id_get (val);
			if (loglevel < 0)
				loglevel = SYSLOGLEVEL;

			log_printf(LOG_ERR, "<fence_xvmd log_level=\"%s\".. option is depracated\n", val);

			if (!*debug) {
				if (loglevel == LOG_LEVEL_DEBUG)
					*debug = 1;

				logsys_config_priority_set (loglevel);
			}

			free(val);
			val = NULL;
		}
	} else
		logsys_config_priority_set (LOG_LEVEL_DEBUG);

	if (ccs_get(ccsfd, "/cluster/logging/@to_stderr", &val) == 0) {
		if(!strcmp(val, "yes")) {
			logmode |= LOG_MODE_OUTPUT_STDERR;
		} else 
		if(!strcmp(val, "no")) {
			logmode &= ~LOG_MODE_OUTPUT_STDERR;
		} else
			log_printf(LOG_ERR, "to_stderr: unknown value\n");
		free(val);
		val = NULL;
	}

	if (ccs_get(ccsfd, "/cluster/logging/@to_syslog", &val) == 0) {
		if(!strcmp(val, "yes")) {
			logmode |= LOG_MODE_OUTPUT_SYSLOG_THREADED;
		} else 
		if(!strcmp(val, "no")) {
			logmode &= ~LOG_MODE_OUTPUT_SYSLOG_THREADED;
		} else
			log_printf(LOG_ERR, "to_syslog: unknown value\n");
		free(val);
		val = NULL;
	}

	if (ccs_get(ccsfd, "/cluster/logging/@to_file", &val) == 0) {
		if(!strcmp(val, "yes")) {
			logmode |= LOG_MODE_OUTPUT_FILE;
		} else 
		if(!strcmp(val, "no")) {
			logmode &= ~LOG_MODE_OUTPUT_FILE;
		} else
			log_printf(LOG_ERR, "to_file: unknown value\n");
		free(val);
		val = NULL;
	}

	if (ccs_get(ccsfd, "/cluster/logging/@filename", &val) == 0) {
		if(logsys_config_file_set(&error, val))
			log_printf(LOG_ERR, "filename: unable to open %s for logging\n", val);
		free(val);
		val = NULL;
	} else
		log_printf(LOG_DEBUG, "filename: use default built-in log file: %s\n", LOGDIR "/fence_xvmd.log");

	if (ccs_get(ccsfd, "/cluster/logging/@syslog_facility", &val) == 0) {
		facility = logsys_facility_id_get (val);
		if (facility < 0) {
			log_printf(LOG_ERR, "syslog_facility: unknown value\n");
			facility = SYSLOGFACILITY;
		}

		logsys_config_facility_set ("XVM", facility);
		free(val);
	} else
	if (ccs_get(ccsfd, "/cluster/fence_xvmd/@log_facility", &val) == 0) {
		facility = logsys_facility_id_get (val);
		if (facility < 0) {
			log_printf(LOG_ERR, "syslog_facility: unknown value\n");
			facility = SYSLOGFACILITY;
		}

		log_printf(LOG_ERR, "<fence_xvmd log_facility=\"%s\".. option is depracated\n", val);

		logsys_config_facility_set ("XVM", facility);
		free(val);
		val = NULL;
	}

	log_config_done(logmode);

	ccs_disconnect(ccsfd);

	return 0;
}


int
main(int argc, char **argv)
{
	fence_xvm_args_t args;
	int mc_sock;
	unsigned int logmode = 0;
	char key[MAX_KEY_LEN];
	int key_len = 0, x;
	char *my_options = "dfi:a:p:C:U:c:k:u?hLXV";
	cman_handle_t ch = NULL;
	void *h = NULL;

	args_init(&args);
	args_get_getopt(argc, argv, my_options, &args);

	if (args.flags & F_HELP) {
		args_usage(argv[0], my_options, 0);

		printf("Arguments may be specified as part of the\n");
		printf("fence_xvmd tag in cluster.conf in the form of:\n");
		printf("    <fence_xvmd argname=\"value\" ... />\n\n");

		args_usage(argv[0], my_options, 1);
		return 0;
	}

	if (args.flags & F_VERSION) {
		printf("%s %s\n", basename(argv[0]), XVM_VERSION);
#ifdef RELEASE_VERSION
		printf("fence release %s\n", RELEASE_VERSION);
#endif
		exit(0);
	}

	if (!(args.flags & F_NOCCS)) {
		args_get_ccs(my_options, &args);
		get_logsys_config_data(&args.debug);
	} else {
		logmode = logsys_config_mode_get();
		logmode &= ~LOG_MODE_DISPLAY_FILELINE;
		log_config_done(logmode);
	}

	args_finalize(&args);
	if (args.debug > 0) {
		dset(args.debug);
                logsys_config_priority_set (LOG_LEVEL_DEBUG);
		args_print(&args);
	}

	if (args.flags & F_ERR) {
		return 1;
	}

	if (args.auth != AUTH_NONE || args.hash != HASH_NONE) {
		key_len = read_key_file(args.key_file, key, sizeof(key));
		if (key_len < 0) {
			log_printf(LOG_WARNING,
				   "Could not read %s; operating without "
			           "authentication\n", args.key_file);
			args.auth = AUTH_NONE;
			args.hash = HASH_NONE;
		}
	}

	/* Fork in to background */
	/* XXX need to wait for child to successfully start before
	   exiting... */
	if (!(args.flags & F_FOREGROUND)) {
		if(daemon(0,0)) {
			log_printf(LOG_ERR, "Could not daemonize\n");
			return 1;
		}
	}

	if (virInitialize() != 0) {
		log_printf(LOG_ERR, "Could not initialize libvirt\n");
		return 1;
	}

	/* Initialize NSS; required to do hashing, as silly as that
	   sounds... */
	if (NSS_NoDB_Init(NULL) != SECSuccess) {
		log_printf(LOG_ERR, "Could not initialize NSS\n");
		return 1;
	}
	
	if (!(args.flags & F_NOCLUSTER)) {
		/* Wait for cman to start. */
		x = 0;
		while ((ch = cman_init(NULL)) == NULL) {
			if (!x) {
				dbg_printf(1,
				  "Could not connect to CMAN; retrying...\n");
				x = 1;
			}
			sleep(3);
		}
		if (x)
			dbg_printf(1, "Connected to CMAN\n");
		/* Wait for quorum */
		while (!cman_is_quorate(ch))
			sleep(3);

		/* Wait for openais checkpointing to become available */
		x = 0;
		while ((h = ckpt_init("vm_states", 262144, 4096, 64, 10)) == NULL) {
			if (!x) {
				dbg_printf(1, "Could not initialize saCkPt; retrying...\n");
				x = 1;
			}
			sleep(3);
		}
		if (x)
			dbg_printf(1, "Checkpoint initialized\n");
	}

	if (args.family == PF_INET)
		mc_sock = ipv4_recv_sk(args.addr, args.port);
	else
		mc_sock = ipv6_recv_sk(args.addr, args.port);
	if (mc_sock < 0) {
		log_printf(LOG_ERR,
			   "Could not set up multicast listen socket\n");
		return 1;
	}


	signal(SIGHUP, sighup_handler);
	signal(SIGINT, sigint_handler);
	signal(SIGTERM, sigint_handler);
	signal(SIGQUIT, sigint_handler);
	xvmd_loop(ch, h, mc_sock, &args, key, key_len);

	//malloc_dump_table();

	return 0;
}
