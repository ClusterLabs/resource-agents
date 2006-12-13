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

#define RG_VERBOSE 0x1

#define QSTAT_ONLY 1
#define VERSION_ONLY 2
#define NODEID_ONLY 3


int running = 1;

void
term_handler(int sig)
{
	running = 0;
}


typedef struct {
	int rgl_count;
	rg_state_t rgl_states[0];
} rg_state_list_t;


void
flag_rgmanager_nodes(cluster_member_list_t *cml)
{
	msgctx_t ctx;
	int max = 0, n;
	generic_msg_hdr *msgp;
	fd_set rfds;

	struct timeval tv;

	if (msg_open(MSG_SOCKET, 0, 0, &ctx, 10) < 0)
		return;

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
	while (++x) {
		name = NULL;
		snprintf(buf, sizeof(buf),
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
		snprintf(buf, sizeof(buf),
			 "/cluster/clusternodes/clusternode[%d]/@nodeid", x);
		if (ccs_get(desc, buf, &name) == 0) {
			nodes[x-1].cn_nodeid = atoi(name);
			free(name);
		}

		ret->cml_count = x;
	}

	ccs_disconnect(desc);
	ret->cml_members = nodes;

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
	int x, y;
	cman_node_t *m, *new;

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
			printf("%s not found\n", these->cml_members[x].cn_name);
			/* WTF? It's not in our config */
			printf("realloc %d\n", (int)((all->cml_count+1) *
			       sizeof(cman_node_t)));
			all->cml_members = realloc(all->cml_members,
						   (all->cml_count+1) *
						   sizeof(cman_node_t));
			if (!all->cml_members) {
				perror("realloc");
				exit(1);
			}
			
			new = &all->cml_members[all->cml_count];

			memcpy(new, &these->cml_members[x],
			       sizeof(cman_node_t));

			if (new->cn_member) {
				new->cn_member = FLAG_UP | FLAG_NOCFG;
			} else {
				new->cn_member = FLAG_NOCFG;
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
_txt_rg_state(rg_state_t *rs, cluster_member_list_t *members, int flags)
{
	char owner[31];


	if (rs->rs_state == RG_STATE_STOPPED ||
	    rs->rs_state == RG_STATE_DISABLED ||
	    rs->rs_state == RG_STATE_ERROR ||
	    rs->rs_state == RG_STATE_FAILED) {

		snprintf(owner, sizeof(owner), "(%-.28s)",
			 my_memb_id_to_name(members, rs->rs_last_owner));
	} else {

		snprintf(owner, sizeof(owner), "%-.30s",
			 my_memb_id_to_name(members, rs->rs_owner));
	}
	printf("  %-20.20s %-30.30s %-16.16s\n",
	       rs->rs_name,
	       owner,
	       rg_state_str(rs->rs_state));
}


void
_txt_rg_state_v(rg_state_t *rs, cluster_member_list_t *members, int flags)
{
	printf("Service Name      : %s\n", rs->rs_name);
	printf("  Current State   : %s (%d)\n",
	       rg_state_str(rs->rs_state), rs->rs_state);
	printf("  Owner           : %s\n",
	       my_memb_id_to_name(members, rs->rs_owner));
	printf("  Last Owner      : %s\n",
	       my_memb_id_to_name(members, rs->rs_last_owner));
	printf("  Last Transition : %s\n",
	       ctime((time_t *)(&rs->rs_transition)));
}


void
txt_rg_state(rg_state_t *rs, cluster_member_list_t *members, int flags)
{
	if (flags & RG_VERBOSE) 
		_txt_rg_state_v(rs, members, flags);
	else
		_txt_rg_state(rs, members, flags);
}


void
xml_rg_state(rg_state_t *rs, cluster_member_list_t *members, int flags)
{
	char time_str[32];
	int x;

	/* Chop off newlines */
	ctime_r((time_t *)&rs->rs_transition, time_str);
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
txt_rg_states(rg_state_list_t *rgl, cluster_member_list_t *members, 
	      char *svcname, int flags)
{
	int x;

	if (!rgl || !members)
		return;

	if (!(flags & RG_VERBOSE)) {
		printf("  %-20.20s %-30.30s %-14.14s\n",
		       "Service Name", "Owner (Last)", "State");
		printf("  %-20.20s %-30.30s %-14.14s\n",
		       "------- ----", "----- ------", "-----");
	} else {
		printf("Service Information\n"
		       "------- -----------\n\n");
	}

	for (x = 0; x < rgl->rgl_count; x++) {
		if (svcname &&
		    strcmp(rgl->rgl_states[x].rs_name, svcname))
			continue;
		txt_rg_state(&rgl->rgl_states[x], members, flags);
	}
}


void
xml_rg_states(rg_state_list_t *rgl, cluster_member_list_t *members,
	      char *svcname)
{
	int x;

	if (!rgl || !members)
		return;

	printf("  <groups>\n");

	for (x = 0; x < rgl->rgl_count; x++) {
		if (svcname &&
		    strcmp(rgl->rgl_states[x].rs_name, svcname))
			continue;
		xml_rg_state(&rgl->rgl_states[x], members, 0);
	}

	printf("  </groups>\n");
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
txt_member_state(cman_node_t *node)
{
	printf("  %-34.34s %4d ", node->cn_name,
	       node->cn_nodeid);

	if (node->cn_member & FLAG_UP)
		printf("Online");
	else
		printf("Offline");

	if (node->cn_member & FLAG_LOCAL)
		printf(", Local");
	
	if (node->cn_member & FLAG_NOCFG)
		printf(", Estranged");

	if (node->cn_member & FLAG_RGMGR)
		printf(", rgmanager");

	printf("\n");
		

}


void
xml_member_state(cman_node_t *node)
{
	printf("    <node name=\"%s\" state=\"%d\" local=\"%d\" "
	       "estranged=\"%d\" rgmanager=\"%d\" nodeid=\"0x%08x\"/>\n",
	       node->cn_name,
	       !!(node->cn_member & FLAG_UP),
	       !!(node->cn_member & FLAG_LOCAL),
	       !!(node->cn_member & FLAG_NOCFG),
	       !!(node->cn_member & FLAG_RGMGR),
	       (uint32_t)((node->cn_nodeid      )&0xffffffff));
}


void
txt_member_states(cluster_member_list_t *membership, char *name)
{
	int x;

	printf("  %-34.34s %-4.4s %s\n", "Member Name", "ID", "Status");
	printf("  %-34.34s %-4.4s %s\n", "------ ----", "----", "------");

	for (x = 0; x < membership->cml_count; x++) {
		if (name && strcmp(membership->cml_members[x].cn_name, name))
			continue;
		txt_member_state(&membership->cml_members[x]);
	}

	printf("\n");
}


void
xml_member_states(cluster_member_list_t *membership, char *name)
{
	int x;

	if (!membership)
		return;

	printf("  <nodes>\n");
	for (x = 0; x < membership->cml_count; x++) {
		if (name && strcmp(membership->cml_members[x].cn_name, name))
			continue;
		xml_member_state(&membership->cml_members[x]);
	}
	printf("  </nodes>\n");
}


void
txt_cluster_status(int qs, cluster_member_list_t *membership,
		   rg_state_list_t *rgs, char *name, char *svcname, 
		   int flags)
{
	if (!svcname && !name) {
		txt_quorum_state(qs);
		if (!membership) {
			/* XXX Check for rgmanager?! */
			printf("Resource Group Manager not running; "
			       "no service information available.\n\n");
		}
	}

	if (!svcname || (name && svcname))
		txt_member_states(membership, name);
	if (!name || (name && svcname))
		txt_rg_states(rgs, membership, svcname, flags);
}


void
xml_cluster_status(int qs, cluster_member_list_t *membership,
		   rg_state_list_t *rgs, char *name, char *svcname,
		   int flags)
{
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
		xml_quorum_state(qs);
	if (!svcname || (name && svcname)) 
		xml_member_states(membership, name);
	if (rgs &&
	    (!name || (name && svcname)))
		xml_rg_states(rgs, membership, svcname);
	printf("</clustat>\n");
}


void
dump_node(cman_node_t *node)
{
	printf("Node %s state %02x\n", node->cn_name, node->cn_member);
}


void 
dump_nodes(cluster_member_list_t *nodes)
{
	int x;

	for (x=0; x<nodes->cml_count; x++) {
		dump_node(&nodes->cml_members[x]);
	}
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
	int fd, qs, ret = 0;
	cluster_member_list_t *membership;
	rg_state_list_t *rgs = NULL;
	int local_node_id;
	int fast = 0;
	int runtype = 0;
	cman_handle_t ch = NULL;

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
				snprintf(real_rg_name,
					 sizeof(real_rg_name),
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
		printf("CMAN is not running.\n");
		return 1;
	}

	switch(runtype) {
	case QSTAT_ONLY:
		if (fd < 0)
		       break;
		ret = !(cman_is_quorate(ch));
		goto cleanup;
	case VERSION_ONLY:
		printf("%s version %s\n", basename(argv[0]),
		       PACKAGE_VERSION);
		if (fd < 0)
		       break;
		goto cleanup;
	case NODEID_ONLY:
		if (fd < 0)
		       break;
		local_node_id = get_my_nodeid(ch);
		printf("0x%08x\n",(uint32_t)(local_node_id)); 
		goto cleanup;
	}

	if (fd < 0) {
		printf("Could not connect to cluster service\n");
		return 1;
	}

	/* XXX add member/rg single-shot state */
	signal(SIGINT, term_handler);
	signal(SIGTERM, term_handler);

	while (1) {
		qs = cman_is_quorate(ch);
		membership = build_member_list(ch, &local_node_id);
		
		rgs = rg_state_list(local_node_id, fast);
		if (rgs) {
			flag_rgmanager_nodes(membership);
		}

		if (refresh_sec) {
			setupterm((char *) 0, STDOUT_FILENO, (int *) 0);
			tputs(clear_screen, lines > 0 ? lines : 1, putchar);
		}

		if (xml)
			xml_cluster_status(qs, membership, rgs, member_name,
					   rg_name,flags);
		else
			txt_cluster_status(qs, membership, rgs, member_name,
					   rg_name,flags);

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
