/*
 * Author: Lon Hohberger <lhh at redhat.com>
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
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
#include <limits.h>
#include <pthread.h>
#define SYSLOG_NAMES
#include <syslog.h>
#include <virterror.h>
#include <nss.h>
#include <libgen.h>
#include <ccs.h>
#include <liblogthread.h>

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

#define LOG_DAEMON_NAME  "xvmd"
#define LOG_MODE_DEFAULT LOG_MODE_OUTPUT_SYSLOG|LOG_MODE_OUTPUT_FILE
static int log_mode_default = LOG_MODE_DEFAULT;

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
			logt_print(LOG_ERR,
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
			logt_print(LOG_ERR, "Failed to call back %s\n", buf);
			return -1;
		}
		break;
	default:
		dbg_printf(1, "Family = %d\n", req->family);
		return -1;
	}

	/* Noops if auth == AUTH_NONE */
	if (tcp_response(fd, auth, key, key_len, 10) <= 0) {
		logt_print(LOG_ERR, "Failed to respond to challenge\n");
		close(fd);
		return -1;
	}

	if (tcp_challenge(fd, auth, key, key_len, 10) <= 0) {
		logt_print(LOG_ERR, "Remote failed challenge\n");
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
		logt_print(LOG_ERR,
		   "Error: Unable to retrieve error from connection!\n");
		return;
	}

	logt_print(LOG_ERR, "Error: libvirt #%d domain %d: %s\n", vep->code,
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
	virDomainPtr vdp = NULL, nvdp = NULL;
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
		logt_print(LOG_NOTICE, "Destroying domain %s...\n",
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
			logt_print(LOG_ERR,
				   "virDomainDestroy() failed: %d\n",
				   ret);
			break;
		}

		response = wait_domain(req, vp, 15);

		if (response) {
			logt_print(LOG_ERR,
			   "Domain %s still exists; fencing failed\n",
			   (char *)req->domain);
		}
		break;
	case FENCE_REBOOT:
		logt_print(LOG_NOTICE, "Rebooting domain %s...\n",
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
			logt_print(LOG_ERR,
				   "virDomainDestroy() failed: %d/%d\n",
				   ret, errno);
			if (domain_desc)
				free(domain_desc);
			break;
		}

		response = wait_domain(req, vp, 15);

		if (response) {
			logt_print(LOG_ERR,
				   "Domain %s still exists; fencing failed\n",
				   (char *)req->domain);
		} else if (domain_desc) {
			/* Recreate the domain if possible */
			/* Success... or not? */
			dbg_printf(2, "Calling virDomainCreateLinux()...\n");
			nvdp = virDomainCreateLinux(vp, domain_desc, 0);

			if (nvdp == NULL) {
				/* More recent versions of libvirt or perhaps the
 				   KVM back-end do not let you create a domain from
 				   XML if there is already a defined domain description
 				   with the same name that it knows about.  You must
 				   then call virDomainCreate() */
				dbg_printf(2, "Failed; Trying virDomainCreate()...\n");
				if (virDomainCreate(vdp) < 0) {
					logt_print(LOG_ERR, "Failed to recreate guest"
						   " %s!\n", (char *)req->domain);
				}
			}
			free(domain_desc);
		}
		break;
	}

	dbg_printf(3, "Sending response to caller...\n");
	if (write(fd, &response, 1) < 0) {
		logt_print(LOG_ERR, "Failed to send response to caller: %s\n",
			   strerror(errno));
	}
out:
	if (vdp)
		virDomainFree(vdp);
	if (nvdp)
		virDomainFree(nvdp);
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
		logt_print(LOG_ERR,
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
		logt_print(LOG_ERR, "virConnectOpen failed: %s",
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
				logt_print(LOG_ERR, "Could not read %s; not updating key",
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
			logt_print(LOG_NOTICE, "NOTICE: virConnectOpen(): "
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
			logt_print(LOG_ERR, "recvfrom: %s\n",
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
			logt_print(LOG_ERR, "XXX Unhandled authentication\n");
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

int
ccs_read_old_logging(int ccsfd, int *facility, int *priority)
{
	char query[256];
	char *val;
	int x, ret = 0;

	/* Get log log_facility */
	snprintf(query, sizeof(query), "/cluster/fence_xvmd/@log_facility");
	if (ccs_get(ccsfd, query, &val) == 0) {
		logt_print(LOG_WARNING,
			   "Use of fence_xvmd/@log_facility is deprecated!\n");
		for (x = 0; facilitynames[x].c_name; x++) {
			if (strcasecmp(val, facilitynames[x].c_name))
				continue;
			*facility = facilitynames[x].c_val;
			ret = 1;
			break;
		}
		free(val);
	}

	/* Get log level */
	snprintf(query, sizeof(query), "/cluster/fence_xvmd/@log_level");
	if (ccs_get(ccsfd, query, &val) == 0) {
		logt_print(LOG_WARNING,
			   "Use of fence_xvmd/@log_level is deprecated!\n");
		*priority = atoi(val);
		free(val);
		if (*priority < 0)
			*priority = SYSLOGLEVEL;
		else
			ret = 1;
	}

	return ret;
}


void
conf_logging(int debug, int logmode, int facility, int loglevel,
	     int filelevel, char *fname)
{
	static int _log_config = 0;

	if (debug)
		filelevel = LOG_DEBUG;

	if (_log_config) {
		logt_conf(LOG_DAEMON_NAME, logmode, facility, loglevel,
			  filelevel, fname);
	}

	logt_init(LOG_DAEMON_NAME, logmode, facility, loglevel,
		  filelevel, fname);
	_log_config = 1;
}


/**
  Grab log configuration data from libccs
 */
static int
get_log_config_data(int use_ccs, int debug)
{
	char fname[PATH_MAX];
	int logmode = log_mode_default;
	int facility = SYSLOGFACILITY;
	int loglevel = SYSLOGLEVEL, filelevel = SYSLOGLEVEL;
	int need_close = 0, ccsfd = -1;

	logt_print(LOG_DEBUG, "Loading logging configuration\n");

	if (use_ccs) {
		ccsfd = ccs_connect();
		if (ccsfd < 0) {
			logt_print(LOG_ERR, "Failed to gather logging "
				"configuration; using defaults\n");
			use_ccs = 0;
		} else {
			need_close = 1;
		}
	}

	snprintf(fname, sizeof(fname)-1, LOGDIR "/fence_xvmd.log");

	if (use_ccs) {
		if (ccs_read_old_logging(ccsfd, &facility, &loglevel))
			filelevel = loglevel;

		ccs_read_logging(ccsfd, LOG_DAEMON_NAME, &debug, &logmode,
       		 	&facility, &loglevel, &filelevel, (char *)fname);
	}

	conf_logging(debug, logmode, facility, loglevel, filelevel, fname);

	if (need_close)
		ccs_disconnect(ccsfd);

	return 0;
}


int
main(int argc, char **argv)
{
	fence_xvm_args_t args;
	int mc_sock;
	char key[MAX_KEY_LEN];
	int key_len = 0, x;
	char *my_options = "dfi:a:p:I:C:U:c:k:u?hLXV";
	cman_handle_t ch = NULL;
	void *h = NULL;

	/* Start w/ stderr output only */
	conf_logging(0, LOG_MODE_OUTPUT_STDERR, SYSLOGFACILITY,
		     SYSLOGLEVEL, SYSLOGLEVEL, NULL);

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
		printf("fence release %s\n", RELEASE_VERSION);
		logt_exit();
		exit(0);
	}

	if (!(args.flags & F_NOCCS)) {
		args_get_ccs(my_options, &args);
	}

	if (args.flags & F_FOREGROUND)
		log_mode_default |= LOG_MODE_OUTPUT_STDERR;
	get_log_config_data(!(args.flags & F_NOCCS), args.debug);

	args_finalize(&args);
	if (args.debug > 0) {
		dset(args.debug);
		args_print(&args);
	}

	if (args.flags & F_ERR)
		goto out_fail;

	if (args.auth != AUTH_NONE || args.hash != HASH_NONE) {
		key_len = read_key_file(args.key_file, key, sizeof(key));
		if (key_len < 0) {
			logt_print(LOG_WARNING,
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
		logt_exit();
		if(daemon(0,0)) {
			logt_reinit();
			logt_print(LOG_ERR, "Could not daemonize\n");
			goto out_fail;
		}
		logt_reinit();
	}

	if (virInitialize() != 0) {
		logt_print(LOG_ERR, "Could not initialize libvirt\n");
		goto out_fail;
	}

	/* Initialize NSS; required to do hashing, as silly as that
	   sounds... */
	if (NSS_NoDB_Init(NULL) != SECSuccess) {
		logt_print(LOG_ERR, "Could not initialize NSS\n");
		goto out_fail;
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
		mc_sock = ipv4_recv_sk(args.addr, args.port, args.ifindex);
	else
		mc_sock = ipv6_recv_sk(args.addr, args.port, args.ifindex);
	if (mc_sock < 0) {
		logt_print(LOG_ERR,
			   "Could not set up multicast listen socket\n");
		goto out_fail;
	}


	signal(SIGHUP, sighup_handler);
	signal(SIGINT, sigint_handler);
	signal(SIGTERM, sigint_handler);
	signal(SIGQUIT, sigint_handler);
	xvmd_loop(ch, h, mc_sock, &args, key, key_len);

	//malloc_dump_table();

	return 0;

out_fail:
	logt_exit();
	return 1;
}
