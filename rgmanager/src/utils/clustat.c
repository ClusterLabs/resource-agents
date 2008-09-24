#include <members.h>
#include <message.h>
#include <msgsimple.h>
#include <resgroup.h>
#include <platform.h>
#include <libgen.h>
#include <ncurses.h>
#include <term.h>
#include <rg_types.h>
#include <termios.h>
#include <ccs.h>
#include <libcman.h>
#include <signal.h>

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#define FLAG_UP    0x1
#define FLAG_LOCAL 0x2
#define FLAG_RGMGR 0x4
#define FLAG_NOCFG 0x8	/* Shouldn't happen */
#define FLAG_QDISK 0x10
#define FLAG_RGMAST 0x20 /* for RIND */

#define RG_VERBOSE 0x1

#define QSTAT_ONLY 1
#define VERSION_ONLY 2
#define NODEID_ONLY 3


int running = 1;
int dimx = 80, dimy = 24, stdout_is_tty = 0;
int rgmanager_master_present = 0;

void
term_handler(int sig)
{
	running = 0;
}


typedef struct {
	int rgl_count;
	rg_state_t rgl_states[0];
} rg_state_list_t;


int
rg_name_sort(const void *left, const void *right)
{
	return strcmp(((rg_state_t *)left)->rs_name,
		      ((rg_state_t *)right)->rs_name);
}


int
member_id_sort(const void *left, const void *right)
{
	cman_node_t *l = (cman_node_t *)left;
	cman_node_t *r = (cman_node_t *)right;

	if (l->cn_nodeid < r->cn_nodeid)
		return -1;
	if (l->cn_nodeid > r->cn_nodeid)
		return 1;
	return 0;
}


void
flag_rgmanager_nodes(cluster_member_list_t *cml)
{
	msgctx_t ctx;
	int max = 0, n;
	generic_msg_hdr *msgp;
	fd_set rfds;

	struct timeval tv;

	if (msg_open(MSG_SOCKET, 0, 0, &ctx, 10) < 0) {
		//perror("msg_open");
		return;
	}

	msg_send_simple(&ctx, RG_STATUS_NODE, 0, 0);

	while (1) {
		FD_ZERO(&rfds);
		msg_fd_set(&ctx, &rfds, &max);
		tv.tv_sec = 10;
		tv.tv_usec = 0;

		n = select(max+1, &rfds, NULL, NULL, &tv);
		if (n == 0) {
			fprintf(stderr, "Timed out waiting for a response "
				"from Resource Group Manager\n");
			break;
		}

		if (n < 0) {
			if (errno == EAGAIN ||
			    errno == EINTR)
				continue;
			fprintf(stderr, "Failed to receive "
				"service data: select: %s\n",
				strerror(errno));
			break;
		}

		n = msg_receive_simple(&ctx, &msgp, tv.tv_sec);

		if (n < 0) {
			if (errno == EAGAIN)
				continue;
			perror("msg_receive_simple");
			break;
		}
	        if (n < sizeof(generic_msg_hdr)) {
			printf("Error: Malformed message\n");
			break;
		}

		if (!msgp) {
			printf("Error: no message?!\n");
			break;
		}

		swab_generic_msg_hdr(msgp);

		if (msgp->gh_command == RG_FAIL) {
			printf("Member states unavailable: %s\n", 
			       rg_strerror(msgp->gh_arg1));
			free(msgp);
			msg_close(&ctx);
			return;
		}	

		if (msgp->gh_command == RG_SUCCESS) {
			free(msgp);
			break;
		}

		for (n = 0; n < cml->cml_count; n++) {
			if (cml->cml_members[n].cn_nodeid != msgp->gh_arg1)
				continue;
			cml->cml_members[n].cn_member |= FLAG_RGMGR;
			if (msgp->gh_arg2) {
				rgmanager_master_present = 1;
				cml->cml_members[n].cn_member |= FLAG_RGMAST;
			}
		}

		free(msgp);
		msgp = NULL;
	}

	msg_send_simple(&ctx, RG_SUCCESS, 0, 0);
	msg_close(&ctx);

	return;
}


rg_state_list_t *
rg_state_list(int local_node_id, int fast)
{
	msgctx_t ctx;
	int max = 0, n, x;
	rg_state_list_t *rsl = NULL;
	generic_msg_hdr *msgp = NULL;
	rg_state_msg_t *rsmp = NULL;
	fd_set rfds;

	struct timeval tv;

	if (msg_open(MSG_SOCKET, 0, 0, &ctx, 10) < 0) {
		//perror("msg_open");
		return NULL;
	}

	msg_send_simple(&ctx, RG_STATUS, fast, 0);

	rsl = malloc(sizeof(rg_state_list_t));
	if (!rsl) {
		printf("Try again, out of memory\n");
		exit(0);
	}
	memset(rsl, 0, sizeof(rg_state_list_t));

	while (1) {
		FD_ZERO(&rfds);
		msg_fd_set(&ctx, &rfds, &max);
		tv.tv_sec = 10;
		tv.tv_usec = 0;

		n = select(max+1, &rfds, NULL, NULL, &tv);
		if (n == 0) {
			fprintf(stderr, "Timed out waiting for a response "
				"from Resource Group Manager\n");
			break;
		}

		if (n < 0) {
			if (errno == EAGAIN ||
			    errno == EINTR)
				continue;
			fprintf(stderr, "Failed to receive "
				"service data: select: %s\n",
				strerror(errno));
			break;
		}

		n = msg_receive_simple(&ctx, &msgp, tv.tv_sec);

		if (n < 0) {
			if (errno == EAGAIN)
				continue;
			perror("msg_receive_simple");
			break;
		}
	        if (n < sizeof(generic_msg_hdr)) {
			printf("Error: Malformed message\n");
			break;
		}

		if (!msgp) {
			printf("Error: no message?!\n");
			break;
		}

		swab_generic_msg_hdr(msgp);

		if (msgp->gh_command == RG_FAIL) {
			printf("Service states unavailable: %s\n", 
			       rg_strerror(msgp->gh_arg1));
			msg_close(&ctx);
			return NULL;
		}	

		if (msgp->gh_command == RG_SUCCESS) {
			free(msgp);
			break;
		}

		if (n < sizeof(*rsmp)) {
			msg_close(&ctx);
			return NULL;
		}

		rsmp = (rg_state_msg_t *)msgp;

		swab_rg_state_t(&rsmp->rsm_state);

		rsl->rgl_count++;
		x = sizeof(rg_state_list_t) +
			(sizeof(rg_state_t) * rsl->rgl_count);
		rsl = realloc(rsl, x);
		if (!rsl) {
			printf("Try again; out of RAM\n");
			exit(1);
		}
		
		memcpy(&rsl->rgl_states[(rsl->rgl_count-1)], 
		       &rsmp->rsm_state, sizeof(rg_state_t));

		free(msgp);
		msgp = NULL;
	}

	msg_send_simple(&ctx, RG_SUCCESS, 0, 0);
	msg_close(&ctx);

	if (!rsl->rgl_count) {
		free(rsl);
		return NULL;
	}

	qsort(rsl->rgl_states, rsl->rgl_count, sizeof(rg_state_t),
	      rg_name_sort);

	return rsl;
}


cluster_member_list_t *ccs_member_list(void)
{
	int desc;
	int x;
	char buf[128];
	char *name;
	cluster_member_list_t *ret = NULL;
	cman_node_t *nodes = NULL;

	desc = ccs_connect();
	if (desc < 0) {
		return NULL;
	}

	while ((ret = malloc(sizeof(*ret))) == NULL)
		sleep(1);
	
	x = 0;
	memset(buf, 0, sizeof(buf));

	while (++x) {
		name = NULL;
		snprintf(buf, sizeof(buf)-1,
			"/cluster/clusternodes/clusternode[%d]/@name", x);

		if (ccs_get(desc, buf, &name) != 0)
			break;

		if (!name)
			break;
		if (!strlen(name)) {
			free(name);
			continue;
		}

		if (!nodes) {
			nodes = malloc(x * sizeof(cman_node_t));
			if (!nodes) {
				perror("malloc");
				ccs_disconnect(desc);
				exit(1);
			}
		} else {
			nodes = realloc(nodes, x * sizeof(cman_node_t));
			if (!nodes) {
				perror("realloc");
				ccs_disconnect(desc);
				exit(1);
			}
		}

		memset(&nodes[x-1], 0, sizeof(cman_node_t));
		strncpy(nodes[x-1].cn_name, name,
			sizeof(nodes[x-1].cn_name));
		free(name);

		/* Add node ID */
		snprintf(buf, sizeof(buf)-1,
			 "/cluster/clusternodes/clusternode[%d]/@nodeid", x);
		if (ccs_get(desc, buf, &name) == 0) {
			nodes[x-1].cn_nodeid = atoi(name);
			free(name);
		}

		ret->cml_count = x;
	}

	ccs_disconnect(desc);
	ret->cml_members = nodes;

	qsort(ret->cml_members, ret->cml_count, sizeof(cman_node_t),
	      member_id_sort);

	return ret;
}


void
flag_nodes(cluster_member_list_t *all, cluster_member_list_t *these,
	   uint8_t flag)
{
	int x;
	cman_node_t *m;

	for (x=0; x<all->cml_count; x++) {

		m = memb_name_to_p(these, all->cml_members[x].cn_name);

		if (m && m->cn_member) {
			all->cml_members[x].cn_nodeid = m->cn_nodeid;
			all->cml_members[x].cn_member |= flag;
		}
	}
}


cluster_member_list_t *
add_missing(cluster_member_list_t *all, cluster_member_list_t *these)
{
	int x, y, addflag;
	cman_node_t *m, *nn;

	if (!these)
		return all;

	for (x=0; x<these->cml_count; x++) {

		m = NULL;
        	for (y = 0; y < all->cml_count; y++) {
			if (!strcmp(all->cml_members[y].cn_name,
				    these->cml_members[x].cn_name))
                        	m = &all->cml_members[y];
		}
        	
		if (!m) {
			all->cml_members = realloc(all->cml_members,
						   (all->cml_count+1) *
						   sizeof(cman_node_t));
			if (!all->cml_members) {
				perror("realloc");
				exit(1);
			}
			
			nn = &all->cml_members[all->cml_count];

			memcpy(nn, &these->cml_members[x],
			       sizeof(cman_node_t));
			
			if (nn->cn_nodeid == 0) { /* quorum disk? */
				addflag = FLAG_QDISK;
			} else {
				addflag = FLAG_NOCFG;
			}

			if (nn->cn_member) {
				nn->cn_member = FLAG_UP | addflag;
			} else {
				nn->cn_member = addflag;
			}
			++all->cml_count;

		}
	}

	return all;
}


char *
my_memb_id_to_name(cluster_member_list_t *members, int memb_id)
{
	int x;

	if (memb_id == 0)
		return "none";

	for (x = 0; x < members->cml_count; x++) {
		if (members->cml_members[x].cn_nodeid == memb_id)
			return members->cml_members[x].cn_name;
	}

	return "unknown";
}


void
_txt_rg_state(rg_state_t *rs, cluster_member_list_t *members, int flags,
	      int svcsize, int nodesize, int statsize)
{
	char owner[MAXHOSTNAMELEN+1];
	char *name = rs->rs_name, *ptr;
	int l;

	if (stdout_is_tty) {
		ptr = strchr(rs->rs_name, ':');
		if (ptr) {
			l = (int)(ptr - rs->rs_name);
			if ((l == 7) &&  /* strlen("service") == 7 */
			    (strncmp(rs->rs_name, "service", l) == 0)) 
				name = ptr+1;
		}
	}

	memset(owner, 0, sizeof(owner));

	if (rs->rs_state == RG_STATE_STOPPED ||
	    rs->rs_state == RG_STATE_DISABLED ||
	    rs->rs_state == RG_STATE_ERROR ||
	    rs->rs_state == RG_STATE_FAILED) {

		snprintf(owner, sizeof(owner)-1, "(%-.*s)", nodesize-2,
			 my_memb_id_to_name(members, rs->rs_last_owner));
	} else {

		snprintf(owner, sizeof(owner)-1, "%-.*s", nodesize,
			 my_memb_id_to_name(members, rs->rs_owner));
	}
	
	printf(" %-*.*s %-*.*s %-*.*s\n",
	       svcsize, svcsize, rs->rs_name,
	       nodesize, nodesize, owner,
	       statsize, statsize, rg_state_str(rs->rs_state));
}


void
_txt_rg_state_v(rg_state_t *rs, cluster_member_list_t *members, int flags)
{
	time_t t;

	printf("Service Name      : %s\n", rs->rs_name);
	printf("  Current State   : %s (%d)\n",
	       rg_state_str(rs->rs_state), rs->rs_state);
	printf("  Owner           : %s\n",
	       my_memb_id_to_name(members, rs->rs_owner));
	printf("  Last Owner      : %s\n",
	       my_memb_id_to_name(members, rs->rs_last_owner));

	t = (time_t)(rs->rs_transition);
	printf("  Last Transition : %s\n", ctime(&t));
}


void
txt_rg_state(rg_state_t *rs, cluster_member_list_t *members, int flags, int svcsize, 
	     int nodesize, int statsize)
{
	if (flags & RG_VERBOSE) 
		_txt_rg_state_v(rs, members, flags);
	else
		_txt_rg_state(rs, members, flags, svcsize, nodesize, statsize);
}


void
xml_rg_state(rg_state_t *rs, cluster_member_list_t *members, int flags)
{
	char time_str[32];
	int x;
	time_t t;

	/* Chop off newlines */
       	t = (time_t)(rs->rs_transition);
	ctime_r(&t, time_str);
	for (x = 0; time_str[x]; x++) {
		if (time_str[x] < 32) {
			time_str[x] = 0;
			break;
		}
	}

	printf("    <group name=\"%s\" state=\"%d\" state_str=\"%s\" "
	       " owner=\"%s\" last_owner=\"%s\" restarts=\"%d\""
	       " last_transition=\"%llu\" last_transition_str=\"%s\"/>\n",
	       rs->rs_name,
	       rs->rs_state,
	       rg_state_str(rs->rs_state),
	       my_memb_id_to_name(members, rs->rs_owner),
	       my_memb_id_to_name(members, rs->rs_last_owner),
	       rs->rs_restarts,
	       (long long unsigned)rs->rs_transition,
	       time_str);
}


void
build_service_field_sizes(int cols, int *svcsize, int *nodesize, int *statsize)
{
	/* Based on 80 columns */
	*svcsize = 30;
	*nodesize = 30;
	*statsize = 14;	/* uninitialized */
	int pad = 6;		/* Spaces and such; newline */

	*svcsize = (cols - (*statsize + pad)) / 2;
	*nodesize = (cols - (*statsize + pad)) / 2;
	if (*svcsize > MAXHOSTNAMELEN)
		*svcsize = MAXHOSTNAMELEN;
	if (*nodesize > MAXHOSTNAMELEN)
		*nodesize = MAXHOSTNAMELEN;
}


int
txt_rg_states(rg_state_list_t *rgl, cluster_member_list_t *members, 
	      char *svcname, int flags)
{
	int x, ret = 0, svcsize, nodesize, statsize;

	if (!rgl || !members)
		return -1;

	if (svcname)
		ret = -1;

	build_service_field_sizes(dimx, &svcsize, &nodesize, &statsize);

	if (!(flags & RG_VERBOSE)) {

		printf(" %-*.*s %-*.*s %-*.*s\n",
		       svcsize, svcsize, "Service Name",
		       nodesize, nodesize, "Owner (Last)",
	       	       statsize, statsize, "State");
		printf(" %-*.*s %-*.*s %-*.*s\n",
		       svcsize, svcsize, "------- ----",
		       nodesize, nodesize, "----- ------",
		       statsize, statsize, "-----");
	} else {
		printf("Service Information\n"
		       "------- -----------\n\n");
	}

	for (x = 0; x < rgl->rgl_count; x++) {
		if (svcname &&
		    strcmp(rgl->rgl_states[x].rs_name, svcname))
			continue;
		txt_rg_state(&rgl->rgl_states[x], members, flags,
			     svcsize, nodesize, statsize);
		if (svcname) {
			switch (rgl->rgl_states[x].rs_state) {
			case RG_STATE_STARTING:
			case RG_STATE_STARTED:
			case RG_STATE_STOPPING:
				ret = 0;
				break;
			default:
				ret = rgl->rgl_states[x].rs_state;
			}
		}
	}

	return ret;
}


int
xml_rg_states(rg_state_list_t *rgl, cluster_member_list_t *members,
	      char *svcname, int flags)
{
	int x;
	int ret = 0;

	if (!rgl || !members)
		return -1;

	printf("  <groups>\n");

	for (x = 0; x < rgl->rgl_count; x++) {
		if (svcname &&
		    strcmp(rgl->rgl_states[x].rs_name, svcname))
			continue;
		xml_rg_state(&rgl->rgl_states[x], members, flags);
		if (svcname) {
			switch (rgl->rgl_states[x].rs_state) {
			case RG_STATE_STARTING:
			case RG_STATE_STARTED:
			case RG_STATE_STOPPING:
				break;
			default:
				ret = rgl->rgl_states[x].rs_state;
			}
		}
	}

	printf("  </groups>\n");
	return ret;
}


void
txt_quorum_state(int qs)
{
	printf("Member Status: ");

	if (qs) {
		printf("Quorate\n\n");
	} else {
		printf("Inquorate\n\n");
	}
}


void
txt_cluster_info(cman_cluster_t *ci) 
{
	time_t now = time(NULL);

	printf("Cluster Status for %s @ %s",
	       ci->ci_name, ctime(&now));
}


void
xml_cluster_info(cman_cluster_t *ci) 
{
	printf("  <cluster name=\"%s\" id=\"%d\" generation=\"%d\"/>\n",
	       ci->ci_name, ci->ci_number, ci->ci_generation);
}


void
xml_quorum_state(int qs)
{
	/* XXX output groupmember attr (carry over from RHCS4) */
	printf("  <quorum ");

	if (qs & FLAG_UP) {
		printf("quorate=\"1\"");
	} else {
		printf("quorate=\"0\"\n");
	}
	if (qs & FLAG_RGMGR) {
		printf(" groupmember=\"1\"");
	} else {
		printf(" groupmember=\"0\"");
	}

	printf("/>\n");
}

void
build_member_field_size(int cols, int *nodesize)
{
	/* Based on 80 columns */
	*nodesize = 40;

	*nodesize = (cols / 2);
	if (*nodesize > MAXHOSTNAMELEN)
		*nodesize = MAXHOSTNAMELEN;
}


void
txt_member_state(cman_node_t *node, int nodesize)
{
	/* If it's down and not in cluster.conf, don't show it */
	if ((node->cn_member & (FLAG_NOCFG | FLAG_UP)) == FLAG_NOCFG)
		return;

	printf(" %-*.*s ", nodesize, nodesize, node->cn_name);
	printf("%4d ", node->cn_nodeid);

	if (node->cn_member & FLAG_UP)
		printf("Online");
	else
		printf("Offline");

	if (node->cn_member & FLAG_LOCAL)
		printf(", Local");
	
	if (node->cn_member & FLAG_NOCFG)
		printf(", Estranged");
	
	if (node->cn_member & FLAG_RGMGR) {
		if (rgmanager_master_present) {
			if (node->cn_member & FLAG_RGMAST) 
				printf(", RG-Master");
			else
				printf(", RG-Worker");
		} else {
			printf(", rgmanager");
		}
	}
	
	if (node->cn_member & FLAG_QDISK)
		printf(", Quorum Disk");

	printf("\n");
}


void
xml_member_state(cman_node_t *node)
{
	/* If it's down and not in cluster.conf, don't show it */
	if ((node->cn_member & (FLAG_NOCFG | FLAG_UP)) == FLAG_NOCFG)
		return;

	printf("    <node name=\"%s\" state=\"%d\" local=\"%d\" "
	       "estranged=\"%d\" rgmanager=\"%d\" rgmanager_master=\"%d\" "
	       "qdisk=\"%d\" nodeid=\"0x%08x\"/>\n",
	       node->cn_name,
	       !!(node->cn_member & FLAG_UP),
	       !!(node->cn_member & FLAG_LOCAL),
	       !!(node->cn_member & FLAG_NOCFG),
	       !!(node->cn_member & FLAG_RGMGR),
	       !!(node->cn_member & FLAG_RGMAST),
	       !!(node->cn_member & FLAG_QDISK),
	       (uint32_t)((node->cn_nodeid      )&0xffffffff));
}


int
txt_member_states(cluster_member_list_t *membership, char *name)
{
	int x, ret = 0, nodesize;

  	if (!membership) {
  		printf("Membership information not available\n");
 		return -1;
  	}

	build_member_field_size(dimx, &nodesize);

	printf(" %-*.*s", nodesize, nodesize, "Member Name");
	printf("%-4.4s %s\n", "ID", "Status");
	printf(" %-*.*s", nodesize, nodesize, "------ ----");
	printf("%-4.4s %s\n", "----", "------");

	for (x = 0; x < membership->cml_count; x++) {
		if (name && strcmp(membership->cml_members[x].cn_name, name))
			continue;
		txt_member_state(&membership->cml_members[x], nodesize);
 		ret = !(membership->cml_members[x].cn_member & FLAG_UP);
	}

	printf("\n");
	return ret;
}


int
xml_member_states(cluster_member_list_t *membership, char *name)
{
	int x, ret = 0;

	if (!membership) {
		printf("  <nodes/>\n");
		return -1;
	}

	printf("  <nodes>\n");
	for (x = 0; x < membership->cml_count; x++) {
		if (name && strcmp(membership->cml_members[x].cn_name, name))
			continue;
		xml_member_state(&membership->cml_members[x]);
		if (name)
			ret = !(membership->cml_members[x].cn_member & FLAG_UP);
	}
	printf("  </nodes>\n");
	
	return ret;
}


int 
txt_cluster_status(cman_cluster_t *ci,
		   int qs, cluster_member_list_t *membership,
		   rg_state_list_t *rgs, char *name, char *svcname, 
		   int flags)
{
	int ret = 0;
	
	if (!svcname && !name) {
  		txt_cluster_info(ci);
		txt_quorum_state(qs);
		if (!membership) {
			/* XXX Check for rgmanager?! */
			printf("Resource Group Manager not running; "
			       "no service information available.\n\n");
		}
	}

  	if (!svcname || (name && svcname))
 		ret = txt_member_states(membership, name);
 	if (name && !svcname)
 		return ret;
 	if (!name || (name && svcname))
 		ret = txt_rg_states(rgs, membership, svcname, flags);

 	return ret;
}


int
xml_cluster_status(cman_cluster_t *ci, int qs,
		   cluster_member_list_t *membership,
		   rg_state_list_t *rgs, char *name, char *svcname,
		   int flags)
{
 	int ret1 = 0, ret2 = -1;
	int x;

	printf("<?xml version=\"1.0\"?>\n");
	printf("<clustat version=\"4.1.1\">\n");

	if (qs) {
		qs = FLAG_UP;
		if (membership) {
			for (x = 0; x < membership->cml_count; x++) {
				if ((membership->cml_members[x].cn_member &
				     (FLAG_LOCAL|FLAG_RGMGR)) ==
				     (FLAG_LOCAL|FLAG_RGMGR)) {
					qs |= FLAG_RGMGR;
					break;
				}
			}
		}
	}

  	if (!svcname && !name)
  		xml_cluster_info(ci);
  	if (!svcname && !name)
  		xml_quorum_state(qs);
  	if (!svcname || (name && svcname)) 
 		ret1 = xml_member_states(membership, name);
 	
  	if (rgs &&
  	    (!name || (name && svcname)))
 		ret2 = xml_rg_states(rgs, membership, svcname, flags);
  	printf("</clustat>\n");
 	
 	if (name && ret1)
 		return ret1;
 	if (svcname && ret2)
 		return ret2;
 	return 0;
}


cluster_member_list_t *
build_member_list(cman_handle_t ch, int *lid)
{
	cluster_member_list_t *all, *part;
	cman_node_t *m;
	int root = 0;
	int x;

	/* Get all members from ccs, and all members reported by the cluster
	   infrastructure */
	root = (getuid() == 0 || geteuid() == 0);

	part = get_member_list(ch);

	if (root && (all = ccs_member_list())) {

		/* See if our config has anyone missed.  If so, flag
		   them as missing from the config file */
		all = add_missing(all, part);

		/* Flag online nodes */
		flag_nodes(all, part, FLAG_UP);
		free_member_list(part);
	} else {
		/* not root - keep it simple for the next block */
		all = part;
	}

	if (!all) {
		*lid = 0;
		return NULL;
	}

	/* Grab the local node ID and flag it from the list of reported
	   online nodes */
	*lid = get_my_nodeid(ch);
	/* */
	for (x=0; x<all->cml_count; x++) {
		if (all->cml_members[x].cn_nodeid == *lid) {
			m = &all->cml_members[x];
			m->cn_member |= FLAG_LOCAL;
			break;
		}
	}

	return all;
}


void
usage(char *arg0)
{
	printf(
"usage: %s <options>\n"
"    -i <interval>      Refresh every <interval> seconds.  May not be used\n"
"                       with -x.\n"
"    -I                 Display local node ID and exit\n"
"    -m <member>        Display status of <member> and exit\n"
"    -s <service>       Display status of <service> and exit\n"
"    -v                 Display version & cluster plugin and exit\n"
"    -x                 Dump information as XML\n"
"    -Q			Return 0 if quorate, 1 if not (no output)\n"
"    -f			Enable fast clustat reports\n"
"    -l			Use long format for services\n"
"\n", basename(arg0));
}


int
main(int argc, char **argv)
{
	int qs, ret = 0;
	cluster_member_list_t *membership;
	rg_state_list_t *rgs = NULL;
	int local_node_id;
	int fast = 0;
	int runtype = 0;
	time_t now;
	cman_handle_t ch = NULL;
	cman_cluster_t ci;

	int refresh_sec = 0, errors = 0;
	int opt, xml = 0, flags = 0;
	char *member_name = NULL;
	char *rg_name = NULL, real_rg_name[64];

	while ((opt = getopt(argc, argv, "fIls:m:i:xvQh?")) != EOF) {
		switch(opt) {
		case 'v':
			runtype = VERSION_ONLY;
			break;

		case 'I':
			runtype = NODEID_ONLY;
			break;

		case 'i':
			refresh_sec = atoi(optarg);
			if (refresh_sec <= 0)
				refresh_sec = 1;
			break;
		case 'l':
			flags |= RG_VERBOSE;
			break;

		case 'm':
			member_name = optarg;
			break;

		case 'Q':
			/* Return to shell: 0 true, 1 false... */
			runtype = QSTAT_ONLY;
			break;

		case 's':
			rg_name = optarg;
			if (!strchr(rg_name,':')) {
				memset(real_rg_name, 0, sizeof(real_rg_name));
				snprintf(real_rg_name,
					 sizeof(real_rg_name)-1,
					 "service:%s", rg_name);
				rg_name = real_rg_name;
			}
			break;

		case 'x':
			if (refresh_sec) {
				printf("Error: Options '-i' and '-x' are "
				       "mutually exclusive\n");
				ret = 1;
				goto cleanup;
			}

			xml = 1;
			break;
		case 'f':
			++fast;
			break;
		case '?':
		case 'h':
			usage(argv[0]);
			return 0;
			break;
		default:
			errors++;
			break;
		}
	}

	if (errors) {
		usage(argv[0]);
		return 1;
	}

	signal(SIGPIPE, SIG_IGN);

	/* Connect & grab all our info */
	ch = cman_init(NULL);
	if (!ch) {
		perror("Could not connect to CMAN");
		return 1;
	}

	switch(runtype) {
	case QSTAT_ONLY:
		if (!ch)
		       break;
		ret = !(cman_is_quorate(ch));
		goto cleanup;
	case VERSION_ONLY:
		printf("%s version %s\n", basename(argv[0]),
		       RELEASE_VERSION);
		if (!ch)
		       break;
		goto cleanup;
	case NODEID_ONLY:
		if (!ch)
		       break;
		local_node_id = get_my_nodeid(ch);
		printf("0x%08x\n",(uint32_t)(local_node_id)); 
		goto cleanup;
	}

	if (!ch) {
		printf("Could not connect to cluster service\n");
		return 1;
	}

	/* XXX add member/rg single-shot state */
	signal(SIGINT, term_handler);
	signal(SIGTERM, term_handler);

	if (isatty(STDOUT_FILENO)) {
		stdout_is_tty = 1;
		setupterm((char *) 0, STDOUT_FILENO, (int *) 0);
		dimx = tigetnum("cols");
		dimy = tigetnum("lines");
	}

	memset(&ci, 0, sizeof(ci));
	cman_get_cluster(ch, &ci);

	while (1) {
		qs = cman_is_quorate(ch);
		membership = build_member_list(ch, &local_node_id);
		
		if (!member_name)
			rgs = rg_state_list(local_node_id, fast);
		if (rgs) {
			flag_rgmanager_nodes(membership);
		}

		if (refresh_sec) {
			tputs(clear_screen, lines > 0 ? lines : 1, putchar);
			now = time(NULL);
		}

		if (xml)
			ret = xml_cluster_status(&ci, qs, membership, rgs,
						 member_name, rg_name,
						 flags);
		else
			ret = txt_cluster_status(&ci, qs, membership, rgs,
						 member_name, rg_name,
						 flags);

		if (membership)
			free_member_list(membership);
		if (rgs)
			free(rgs);

		if (!refresh_sec || !running)
			break;

		sleep(refresh_sec);
	}

cleanup:
	cman_finish(ch);

	return ret;
}
