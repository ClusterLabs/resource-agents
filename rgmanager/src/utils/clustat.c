#include <magma.h>
#include <magmamsg.h>
#include <msgsimple.h>
#include <resgroup.h>
#include <platform.h>
#include <libgen.h>
#include <ncurses.h>
#include <term.h>
#include <termios.h>

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

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

	if (!rsl->rgl_count) {
		free(rsl);
		return NULL;
	}

	return rsl;
}


void
txt_rg_state(rg_state_t *rs, cluster_member_list_t *members)
{
	if (rs->rs_state == RG_STATE_STOPPED ||
	    rs->rs_state == RG_STATE_DISABLED ||
	    rs->rs_state == RG_STATE_ERROR ||
	    rs->rs_state == RG_STATE_FAILED) {
		printf("  %-20.20s (%-28.28s) %-10.10s\n",
		       rs->rs_name,
		       memb_id_to_name(members, rs->rs_last_owner),
		       rg_state_str(rs->rs_state));
	} else {
		printf("  %-20.20s %-30.30s %-10.10s\n",
		       rs->rs_name,
		       memb_id_to_name(members, rs->rs_owner),
		       rg_state_str(rs->rs_state));
	}
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

	printf("  %-20.20s %-30.30s %-10.10s\n",
	       "Resource Group", "Owner (Last)", "State");
	printf("  %-20.20s %-30.30s %-10.10s\n",
	       "-------- -----", "----- ------", "-----");

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
		printf("Quorate");
	else {
		printf("Inquorate\n");
		return;
	}

	if (qs & QF_GROUPMEMBER)
		printf(", Group Member");

	printf("\n\n");
}


void
xml_quorum_state(int qs)
{
	printf("  <quorum ");

	if (qs & QF_QUORATE)
		printf("quorate=\"1\"");
	else {
		printf("quorate=\"0\" groupmember-\"0\"/>\n");
		return;
	}

	if (qs & QF_GROUPMEMBER)
		printf(" groupmember=\"1\"");

	printf("/>\n");
}


void
txt_member_state(cluster_member_t *node)
{
	char *state;

	if (node->cm_state == STATE_UP)
		state = "Online";
	else
		state = "Offline";

	printf("  %-40.40s %-10s 0x%08x%08x\n", node->cm_name, state,
	       (uint32_t)((node->cm_id >> 32)&0xffffffff),
	       (uint32_t)((node->cm_id      )&0xffffffff));
}


void
xml_member_state(cluster_member_t *node)
{
	printf("    <node name=\"%s\" state=\"%d\" nodeid=\"0x%08x%08x\"/>\n",
	       node->cm_name,
	       node->cm_state,
	       (uint32_t)((node->cm_id >> 32)&0xffffffff),
	       (uint32_t)((node->cm_id      )&0xffffffff));
}


void
txt_member_states(cluster_member_list_t *membership)
{
	int x;

	printf("  %-40.40s %-10s %s\n", "Member Name", "State", "ID");
	printf("  %-40.40s %-10s %s\n", "------ ----", "-----", "--");

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
		printf("Not a member of the Resource Manager service "
		       "group.\n");
		printf("Resource Group information unavailable; showing "
		       "all cluster members.\n\n");
		membership = clu_member_list(NULL);
		txt_member_states(membership);
		cml_free(membership);
	} else {
		txt_member_states(membership);
	}

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
		clu_local_nodeid(RG_SERVICE_GROUP, &local_node_id);
		membership = clu_member_list(RG_SERVICE_GROUP);
		msg_update(membership);
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
