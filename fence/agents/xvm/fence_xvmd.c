/*
  Copyright Red Hat, Inc. 2006

  This program is free software; you can redistribute it and/or modify it
  under the terms of the GNU General Public License as published by the
  Free Software Foundation; either version 2, or (at your option) any
  later version.

  This program is distributed in the hope that it will be useful, but
  WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; see the file COPYING.  If not, write to the
  Free Software Foundation, Inc.,  675 Mass Ave, Cambridge, 
  MA 02139, USA.
*/
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

/* Local includes */
#include "xvm.h"
#include "simple_auth.h"
#include "options.h"
#include "mcast.h"
#include "tcp.h"
#include "virt.h"
#include "libcman.h"

static int running = 1;

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
			printf("Failed to call back\n");
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
			printf("Failed to call back %s\n", buf);
			return -1;
		}
		break;
	default:
		printf("Family = %d\n", req->family);
		return -1;
	}

	/* Noops if auth == AUTH_NONE */
	if (tcp_response(fd, auth, key, key_len, 10) <= 0) {
		printf("Failed to respond to challenge\n");
		close(fd);
		return -1;
	}

	if (tcp_challenge(fd, auth, key, key_len, 10) <= 0) {
		printf("Remote failed challenge\n");
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
		printf("Error: Unable to retrieve error from connection!\n");
		return;
	}

	printf("Error: libvirt #%d domain %d: %s\n", vep->code,
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


/*
   Nuke the OS block if this domain was booted using a bootloader.
   XXX We probably should use libxml2 to do this, but this is very fast
 */
void
cleanup_xmldesc(char *xmldesc)
{
	char *start = NULL;
	char *end = NULL;

#define STARTBOOTTAG "<bootloader>"
#define ENDBOOTTAG   "</bootloader>"
#define STARTOSTAG   "<os>"
#define ENDOSTAG     "</os>"

	/* Part 1: Check for a boot loader */
	start = strcasestr(xmldesc, STARTBOOTTAG);
	if (start) {
		start += strlen(STARTBOOTTAG);
		end = strcasestr(start, ENDBOOTTAG);
		if (end == start) {
			/* Empty bootloader tag -> return */
			return;
		}
	}

	/* Part 2: Nuke the <os> tag */
	start = strcasestr(xmldesc, STARTOSTAG);
	if (!start)
		return;
	end = strcasestr(start, ENDOSTAG);
	if (!end)
		return;
	end += strlen(ENDOSTAG);

	memset(start, ' ', end-start);
}


int
do_fence_request_tcp(fence_req_t *req, fence_auth_type_t auth,
		     void *key, size_t key_len, virConnectPtr vp)
{
	int fd, ret = -1;
	virDomainPtr vdp;
	char response = 1;
	char *domain_desc;

	if (!(vdp = get_domain(req, vp)))
		goto out;

	fd = connect_tcp(req, auth, key, key_len);
	if (fd < 0)
		goto out;

	switch(req->request) {
	case FENCE_NULL:
		printf("NULL operation: returning failure\n");
		response = 1;
		break;
	case FENCE_OFF:
		printf("Destroying domain %s...\n", (char *)req->domain);
		ret = virDomainDestroy(vdp);
		if (ret < 0) {
			/* raise_error(vp); */
			break;
		} else { 
			sleep(1);
		}

		/* Check domain liveliness.  If the domain is still here,
		   we return failure, and the client must then retry */
		/* XXX On the xen 3.0.4 API, we will be able to guarantee
		   synchronous virDomainDestroy, so this check will not
		   be necessary */
		vdp = get_domain(req, vp);
		if (!vdp) {
			response = 0;	/* Success! */
		} else {
			virDomainFree(vdp);
		}
		break;
	case FENCE_REBOOT:
		printf("Rebooting domain %s...\n",
		       (char *)req->domain);
		domain_desc = virDomainGetXMLDesc(vdp, 0);

		if (domain_desc)
			cleanup_xmldesc(domain_desc);
		ret = virDomainDestroy(vdp);
		if (ret < 0) {
			if (domain_desc)
				free(domain_desc);
			break;
		} else {
			/* Give it time for the operation to complete */
			sleep(3);
		}

		/* Check domain liveliness.  If the domain is still here,
		   we return failure, and the client must then retry */
		/* XXX On the xen 3.0.4 API, we will be able to guarantee
		   synchronous virDomainDestroy, so this check will not
		   be necessary */
		vdp = get_domain(req, vp);
		if (!vdp) {
			response = 0;	/* Success! */
		} else {
			virDomainFree(vdp);
			ret = 1;	/* Failed to kill it */
		}

		/* Recreate the domain if possible */
		if (ret == 0 && domain_desc) {
			/* Success */
			virDomainCreateLinux(vp, domain_desc, 0);
		}

		if (domain_desc)
			free(domain_desc);
		break;
	}
	
	if (write(fd, &response, 1) < 0) {
		perror("write");
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
		if (nodes[x].cn_nodeid > high)
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
	if (!strcmp("Domain-0", (char *)domain))
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
		printf("Storing %s\n", vl->vm_states[x].v_name);
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
		printf("Storing %s\n", vl->vm_states[x].v_uuid);
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
		printf("XXX I have no record of that domain\n");
		return;
	}

	printf("Evaluating Domain: %s   Last Owner: %d   State %d\n",
	       data->domain, vst.s_owner, vst.s_state);
			
	if (get_cman_ids(ch, NULL, &high_id) < 0)
		return;

	if (my_id == high_id && vst.s_owner != my_id) {

		memset(&node, 0, sizeof(node));
		cman_get_node(ch, vst.s_owner, &node);
		if (node.cn_nodeid == 0) {
			printf("Node %d does not exist\n", vst.s_owner);
			return;
		}

		if (node.cn_member) {
			printf("Node %d is online - not taking action\n",
			       vst.s_owner);
			return;
		}

		fenced = 0;
		cman_get_fenceinfo(ch, vst.s_owner, &fence_time, &fenced, NULL);
		if (fenced == 0) {
			printf("Node %d is dead but not fenced - not "
			       "taking action\n", vst.s_owner);
			return;
		}

		printf("Node %d is dead & fenced\n", vst.s_owner);
		ret = 0;
					
	} else if (vst.s_owner == my_id) {
		printf("I am the last owner of the domain\n");
		ret = 0;
	}

	if (!ret) {
		switch(auth) {
		case AUTH_NONE:
		case AUTH_SHA1:
		case AUTH_SHA256:
		case AUTH_SHA512:
			printf("Plain TCP request\n");
			do_notify_caller_tcp(data, auth, key, key_len, ret);
			break;
		default:
			printf("XXX Unhandled authentication\n");
		}
	}
}


int
xvmd_loop(void *h, int fd, fence_xvm_args_t *args,
	  void *key, size_t key_len)
{
	fd_set rfds;
	struct timeval tv;
	struct sockaddr_in sin;
	int len;
	int n;
	int my_id;
	socklen_t slen;
	fence_req_t data;
	virConnectPtr vp;
	virt_list_t *vl;
	virt_state_t *dom;
	cman_handle_t ch;

	vp = virConnectOpen(NULL);
	if (!vp) {
		perror("virConnectOpen");
		return -1;
	}

	ch = cman_init(NULL);

	get_cman_ids(ch, &my_id, NULL);
	printf("My Node ID = %d\n", my_id);

	vl = vl_get(vp, my_id);
	vl_print(vl);

	while (running) {
		FD_ZERO(&rfds);
		FD_SET(fd, &rfds);
		tv.tv_sec = 10;
		tv.tv_usec = 0;

		n = select(fd+1, &rfds, NULL, NULL, &tv);
		if (n < 0)
			continue;
		if (n == 0) {
			virt_list_update(vp, &vl, my_id);
			vl_print(vl);
			/* Store information here */
			if (args->flags & F_USE_UUID) 
				store_domains_by_uuid(h, vl);
			else
				store_domains_by_name(h, vl);
			continue;
		}

		slen = sizeof(sin);
		len = recvfrom(fd, &data, sizeof(data), 0,
			       (struct sockaddr *)&sin, &slen);
		
		if (len <= 0) {
			perror("recvfrom");
			continue;
		}

		if (!verify_request(&data, args->hash, key, key_len)) {
			printf("Key mismatch; dropping packet\n");
			continue;
		}

		if ((args->flags & F_USE_UUID) &&
		    !(data.flags & RF_UUID)) {
			printf("Dropping packet: Request to fence by "
			       "name while using UUIDs\n");
			continue;
		}

		if (!(args->flags & F_USE_UUID) &&
		    (data.flags & RF_UUID)) {
			printf("Dropping packet: Request to fence by "
			       "UUID while using names\n");
			continue;
		}

		printf("Request to fence: %s\n", data.domain);

		/* Do a quick update to make sure we're current */
		virt_list_update(vp, &vl, my_id);
		vl_print(vl);

		if (args->flags & F_USE_UUID)
			dom = vl_find_uuid(vl, (char *)data.domain);
		else
			dom = vl_find_name(vl, (char *)data.domain);
		if (!dom) {
			handle_remote_domain(ch, h, &data, args->auth,
					     key, key_len, my_id);
			continue;
		}

		printf("%s is running locally\n",
		       (char *)data.domain);

		switch(args->auth) {
		case AUTH_NONE:
		case AUTH_SHA1:
		case AUTH_SHA256:
		case AUTH_SHA512:
			printf("Plain TCP request\n");
			do_fence_request_tcp(&data, args->auth, key,
					     key_len, vp);
			break;
		default:
			printf("XXX Unhandled authentication\n");
		}
	}

	cman_finish(ch);
	virConnectClose(vp);

	return 0;
}


int
main(int argc, char **argv)
{
	fence_xvm_args_t args;
	int mc_sock;
	char key[4096];
	int key_len;
	char *my_options = "dfi:a:p:C:c:k:u?hV";
	void *h;

	args_init(&args);
	args_get_getopt(argc, argv, my_options, &args);
	args_finalize(&args);
	if (args.flags & F_DEBUG)
		args_print(&args);

	if (args.flags & F_ERR) {
		args_usage(argv[0], my_options, 0);
		return 1;
	}

	if (args.flags & F_HELP) {
		args_usage(argv[0], my_options, 0);
		return 0;
	}

	if (args.flags & F_VERSION) {
		printf("%s %s\n", basename(argv[0]), XVM_VERSION);
#ifdef FENCE_RELEASE_NAME
		printf("fence release %s\n", FENCE_RELEASE_NAME);
#endif
		exit(0);
	}

	key_len = read_key_file(args.key_file, key, sizeof(key));
	if (key_len < 0) {
		printf("Could not read key file\n");
		return 1;
	}

	/* Fork in to background */
	/* XXX need to wait for child to successfully start before
	   exiting... */
	if (!(args.flags & F_FOREGROUND))
		daemon(0,0);

	if (virInitialize() != 0) {
		printf("Could not initialize libvirt\n");
		return 1;
	}

	/* Initialize NSS; required to do hashing, as silly as that
	   sounds... */
	if (NSS_NoDB_Init(NULL) != SECSuccess) {
		printf("Could not initialize NSS\n");
		return 1;
	}

	h = ckpt_init("vm_states", 262144, 4096, 64, 10);
	if (!h) {
		printf("Could not initialize Checkpoint\n");
		return 1;
	}

	if (args.family == PF_INET)
		mc_sock = ipv4_recv_sk(args.addr, args.port);
	else
		mc_sock = ipv6_recv_sk(args.addr, args.port);
	if (mc_sock < 0) {
		printf("Could not set up multicast listen socket\n");
		return 1;
	}

	xvmd_loop(h, mc_sock, &args, key, key_len);

	return 0;
}
