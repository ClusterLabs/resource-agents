#include <magma.h>
#include <magmamsg.h>
#include <msgsimple.h>
#include <resgroup.h>
#include <platform.h>
#include <libgen.h>
#include <ncurses.h>
#include <term.h>
#include <termios.h>
#include <ccs.h>

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#define FLAG_LOCAL 0x1
#define FLAG_UP    0x2
#define FLAG_RGMGR 0x4
#define FLAG_NOCFG 0x8	/* Shouldn't happen */


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


rg_state_list_t *
rg_state_list(uint64_t local_node_id)
{
	int fd, n, x;
	rg_state_list_t *rsl = NULL;
	generic_msg_hdr *msgp = NULL;
	rg_state_msg_t *rsmp = NULL;

	fd = msg_open(local_node_id, RG_PORT, RG_PURPOSE, 2);
	if (fd == -1) {
		return NULL;
	}

	msg_send_simple(fd, RG_STATUS, 0, 0);

	rsl = malloc(sizeof(rg_state_list_t));
	if (!rsl) {
		printf("Try again, out of memory\n");
		exit(0);
	}
	memset(rsl, 0, sizeof(rg_state_list_t));

	while (1) {
		n = msg_receive_simple(fd, &msgp, 10);
	        if (n < sizeof(generic_msg_hdr))
			break;

		if (!msgp) {
			printf("Error: no message?!\n");
			break;
		}

		swab_generic_msg_hdr(msgp);
		if (msgp->gh_command == RG_SUCCESS) {
			free(msgp);
			break;
		}

		if (n < sizeof(*rsmp)) {
			msg_close(fd);
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

	msg_close(fd);

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

	desc = ccs_connect();
	if (desc < 0) {
		return NULL;
	}

	x = 1;
	
	snprintf(buf, sizeof(buf),
		 "/cluster/clusternodes/clusternode[%d]/@name", x);
	while (ccs_get(desc, buf, &name) == 0) {
		if (!ret) {
			ret = malloc(cml_size(x));
			if (!ret) {
				perror("malloc");
				ccs_disconnect(desc);
				exit(1);
			}
			memset(ret, 0, cml_size(x));
		} else {
			ret = realloc(ret, cml_size(x));
			if (!ret) {
				perror("realloc");
				ccs_disconnect(desc);
				exit(1);
			}
		}

		memset(&ret->cml_members[x-1], 0, sizeof(cluster_member_t));
		strncpy(ret->cml_members[x-1].cm_name, name,
			sizeof(ret->cml_members[x-1].cm_name));
		free(name);

		ret->cml_count = x;
		++x;
		snprintf(buf, sizeof(buf),
			 "/cluster/clusternodes/clusternode[%d]/@name", x);
	}

	ccs_disconnect(desc);
	return ret;
}


void
flag_nodes(cluster_member_list_t *all, cluster_member_list_t *these,
	   uint8_t flag)
{
	int x;
	cluster_member_t *m;

	for (x=0; x<all->cml_count; x++) {

		m = memb_name_to_p(these, all->cml_members[x].cm_name);

		if (m) {
			all->cml_members[x].cm_id = m->cm_id;
			all->cml_members[x].cm_state |= flag;
		}
	}
}


cluster_member_list_t *
add_missing(cluster_member_list_t *all, cluster_member_list_t *these)
{
	int x, y;
	cluster_member_t *m, *new;

	for (x=0; x<these->cml_count; x++) {

		m = NULL;
        	for (y = 0; y < all->cml_count; y++) {
			if (!strcmp(all->cml_members[y].cm_name,
				    these->cml_members[x].cm_name))
                        	m = &all->cml_members[y];
		}

		if (!m) {
			printf("%s not found\n", these->cml_members[x].cm_name);
			/* WTF? It's not in our config */
			printf("realloc %d\n", (int)cml_size((all->cml_count+1)));
			all = realloc(all, cml_size((all->cml_count+1)));
			if (!all) {
				perror("realloc");
				exit(1);
			}
			
			new = &all->cml_members[all->cml_count];

			memcpy(new, &these->cml_members[x],
			       sizeof(cluster_member_t));

			if (new->cm_state == STATE_UP) {
				new->cm_state = FLAG_UP | FLAG_NOCFG;
			} else {
				new->cm_state = FLAG_NOCFG;
			}
			++all->cml_count;

		}
	}

	return all;
}


char *
my_memb_id_to_name(cluster_member_list_t *members, uint64_t memb_id)
{
	int x;

	if (memb_id == NODE_ID_NONE)
		return "unknown";

	for (x = 0; x < members->cml_count; x++) {
		if (members->cml_members[x].cm_id == memb_id)
			return members->cml_members[x].cm_name;
	}

	return "unknown";
}

void
txt_rg_state(rg_state_t *rs, cluster_member_list_t *members)
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
xml_rg_state(rg_state_t *rs, cluster_member_list_t *members)
{
	printf("    <group name=\"%s\" state=\"%d\" state_str=\"%s\" "
	       " owner=\"%s\" last_owner=\"%s\" restarts=\"%d\"/>\n",
	       rs->rs_name,
	       rs->rs_state,
	       rg_state_str(rs->rs_state),
	       memb_id_to_name(members, rs->rs_owner),
	       memb_id_to_name(members, rs->rs_last_owner),
	       rs->rs_restarts);
}


void
txt_rg_states(rg_state_list_t *rgl, cluster_member_list_t *members)
{
	int x;

	if (!rgl || !members)
		return;

	printf("  %-20.20s %-30.30s %-14.14s\n",
	       "Service Name", "Owner (Last)", "State");
	printf("  %-20.20s %-30.30s %-14.14s\n",
	       "------- ----", "----- ------", "-----");

	for (x = 0; x < rgl->rgl_count; x++)
		txt_rg_state(&rgl->rgl_states[x], members);
}


void
xml_rg_states(rg_state_list_t *rgl, cluster_member_list_t *members)
{
	int x;

	if (!rgl || !members)
		return;

	printf("  <groups>\n");

	for (x = 0; x < rgl->rgl_count; x++)
		xml_rg_state(&rgl->rgl_states[x], members);

	printf("  </groups>\n");
}



void
txt_quorum_state(int qs)
{
	printf("Member Status: ");

	if (qs & QF_QUORATE)
		printf("Quorate\n\n");
	else {
		printf("Inquorate\n\n");
	}
}


void
xml_quorum_state(int qs)
{
	printf("  <quorum ");

	if (qs & QF_QUORATE)
		printf("quorate=\"1\"");
	else {
		printf("quorate=\"0\" groupmember=\"0\"/>\n");
		return;
	}

	if (qs & QF_GROUPMEMBER)
		printf(" groupmember=\"1\"");

	printf("/>\n");
}


void
txt_member_state(cluster_member_t *node)
{
	printf("  %-40.40s ", node->cm_name);

	if (node->cm_state & FLAG_UP)
		printf("Online");
	else
		printf("Offline");

	if (node->cm_state & FLAG_LOCAL)
		printf(", Local");
	
	if (node->cm_state & FLAG_NOCFG)
		printf(", Estranged");

	if (node->cm_state & FLAG_RGMGR)
		printf(", rgmanager");

	printf("\n");
		

}


void
xml_member_state(cluster_member_t *node)
{
	printf("    <node name=\"%s\" state=\"%d\" nodeid=\"0x%08x%08x\"/>\n",
	       node->cm_name,
	       node->cm_state &0x1,
	       (uint32_t)((node->cm_id >> 32)&0xffffffff),
	       (uint32_t)((node->cm_id      )&0xffffffff));
}


void
txt_member_states(cluster_member_list_t *membership)
{
	int x;

	printf("  %-40.40s %s\n", "Member Name", "Status");
	printf("  %-40.40s %s\n", "------ ----", "------");

	for (x = 0; x < membership->cml_count; x++)
		txt_member_state(&membership->cml_members[x]);

	printf("\n");
}


void
xml_member_states(cluster_member_list_t *membership)
{
	int x;

	if (!membership)
		return;

	printf("  <nodes>\n");
	for (x = 0; x < membership->cml_count; x++)
		xml_member_state(&membership->cml_members[x]);
	printf("  </nodes>\n");
}


void
txt_cluster_status(int qs, cluster_member_list_t *membership,
		   rg_state_list_t *rgs)
{
	txt_quorum_state(qs);

	if (!membership || !(qs & QF_GROUPMEMBER)) {
		printf("Resource Group Manager not running; no service "
		       "information available.\n\n");
	}

	txt_member_states(membership);
	txt_rg_states(rgs, membership);
}


void
xml_cluster_status(int qs, cluster_member_list_t *membership,
		   rg_state_list_t *rgs)
{
	printf("<?xml version=\"1.0\"?>\n");
	printf("<clustat version=\"4.0\">\n");
	xml_quorum_state(qs);
	xml_member_states(membership);
	if (rgs)
		xml_rg_states(rgs, membership);
	printf("</clustat>\n");
}


void
dump_node(cluster_member_t *node)
{
	printf("Node %s state %02x\n", node->cm_name, node->cm_state);
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
build_member_list(uint64_t *lid)
{
	cluster_member_list_t *all, *part;
	cluster_member_t *m;
	int x;

	/* Get all members from ccs, and all members reported by the cluster
	   infrastructure */
	all = ccs_member_list();
	part = clu_member_list(NULL);
	msg_update(part); /* XXX magmamsg is awful. */

	/* Flag online nodes */
	flag_nodes(all, part, FLAG_UP);

	/* See if our config has anyone missed.  If so, flag them as missing 
	   from the config file */
	all = add_missing(all, part);

	/* Grab the local node ID and flag it from the list of reported
	   online nodes */
	clu_local_nodeid(NULL, lid);
	for (x=0; x<all->cml_count; x++) {
		if (all->cml_members[x].cm_id == *lid) {
			m = &all->cml_members[x];
			m->cm_state |= FLAG_LOCAL;
			break;
		}
	}
			
	cml_free(part);

	/* Flag rgmanager nodes, if any */
	part = clu_member_list(RG_SERVICE_GROUP);
	flag_nodes(all, part, FLAG_RGMGR);
	cml_free(part);

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
"    -s <service>       Display statis of <service> and exit\n"
"    -v                 Display version & cluster plugin and exit\n"
"    -x                 Dump information as XML\n"
"\n", basename(arg0));
}


int
main(int argc, char **argv)
{
	int fd, qs, ret = 0;
	cluster_member_list_t *membership;
	rg_state_list_t *rgs = NULL;
	uint64_t local_node_id;

	int refresh_sec = 0, errors = 0;
	int opt, xml = 0;
	char *member_name;
	char *rg_name;

	/* Connect & grab all our info */
	fd = clu_connect(RG_SERVICE_GROUP, 0);
	if (fd < 0) {
		printf("Could not connect to cluster service\n");
		return 1;
	}
	
	while ((opt = getopt(argc, argv, "Is:m:i:xvQh?")) != EOF) {
		switch(opt) {
		case 'v':
			printf("%s version %s\n", basename(argv[0]),
			       PACKAGE_VERSION);
			printf("Connected via: %s\n", clu_plugin_version());
			goto cleanup;

		case 'I':
			printf("0x%08x%08x\n",(uint32_t)(local_node_id>>32),
			       (uint32_t)(local_node_id&0xffffffff)); 
			goto cleanup;

		case 'i':
			refresh_sec = atoi(optarg);
			if (refresh_sec <= 0)
				refresh_sec = 1;
			break;

		case 'm':
			member_name = optarg;
			break;

		case 'Q':
			/* Return to shell: 0 true, 1 false... */
			ret = !(clu_quorum_status(RG_SERVICE_GROUP) &
				QF_QUORATE);
			goto cleanup;

		case 's':
			rg_name = optarg;
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

	/* XXX add member/rg single-shot state */
	signal(SIGINT, term_handler);
	signal(SIGTERM, term_handler);

	while (1) {
		qs = clu_quorum_status(RG_SERVICE_GROUP);
		membership = build_member_list(&local_node_id);
		
		rgs = rg_state_list(local_node_id);

		if (refresh_sec) {
			setupterm((char *) 0, STDOUT_FILENO, (int *) 0);
			tputs(clear_screen, lines > 0 ? lines : 1, putchar);
		}

		if (xml)
			xml_cluster_status(qs, membership, rgs);
		else
			txt_cluster_status(qs, membership, rgs);

		if (membership)
			cml_free(membership);
		if (rgs)
			free(rgs);

		if (!refresh_sec || !running)
			break;

		sleep(refresh_sec);
	}

cleanup:
	clu_disconnect(fd);
	return ret;
}
