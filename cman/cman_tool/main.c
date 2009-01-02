#include <inttypes.h>
#include <unistd.h>
#include <signal.h>
#include <time.h>
#include <netinet/in.h>
#include "copyright.cf"
#include "libcman.h"
#include "cman_tool.h"

#define DEFAULT_CONFIG_MODULE "xmlconfig"

#define OPTION_STRING		("m:n:v:e:2p:c:r:i:N:t:o:k:F:C:VAPwfqah?Xd::")
#define OP_JOIN			1
#define OP_LEAVE		2
#define OP_EXPECTED		3
#define OP_VOTES		4
#define OP_KILL			5
#define OP_VERSION		6
#define OP_WAIT			7
#define OP_STATUS		8
#define OP_NODES		9
#define OP_SERVICES		10
#define OP_DEBUG		11
#define OP_DUMP_OBJDB		12


static void print_usage(int subcmd)
{
	printf("Usage:\n");
	printf("\n");
	printf("%s <join|leave|kill|expected|votes|version|wait|status|nodes|services|debug> [options]\n",
	       prog_name);
	printf("\n");
	printf("Options:\n");
	printf("  -h               Print this help, then exit\n");
	printf("  -V               Print program version information, then exit\n");
	printf("  -d               Enable debug output\n");
	printf("\n");

	if (!subcmd || subcmd == OP_JOIN) {
		printf("join\n");
		printf("  Cluster & node information is taken from configuration modules.\n");
		printf("  These switches are provided to allow those values to be overridden.\n");
		printf("  Use them with extreme care.\n\n");

		printf("  -m <addr>        Multicast address to use\n");
		printf("  -v <votes>       Number of votes this node has\n");
		printf("  -e <votes>       Number of expected votes for the cluster\n");
		printf("  -p <port>        UDP port number for cman communications\n");
		printf("  -n <nodename>    The name of this node (defaults to hostname)\n");
		printf("  -c <clustername> The name of the cluster to join\n");
		printf("  -N <id>          Node id\n");
		printf("  -C <module>      Config file reader (default: " DEFAULT_CONFIG_MODULE ")\n");
		printf("  -w               Wait until node has joined a cluster\n");
		printf("  -q               Wait until the cluster is quorate\n");
		printf("  -t               Maximum time (in seconds) to wait\n");
		printf("  -k <file>        Private key file for AIS communications\n");
		printf("  -P               Don't set aisexec to realtime priority\n");
		printf("  -X               Use internal cman defaults for configuration\n");
		printf("  -A               Don't load openais services\n");
		printf("\n");
	}


	if (!subcmd || subcmd == OP_WAIT) {
		printf("wait               Wait until the node is a member of a cluster\n");
		printf("  -q               Wait until the cluster is quorate\n");
		printf("  -t               Maximum time (in seconds) to wait\n");
		printf("\n");
	}

	if (!subcmd || subcmd == OP_LEAVE) {
		printf("leave\n");
		printf("  -w               If cluster is in transition, wait and keep trying\n");
		printf("  -t               Maximum time (in seconds) to wait\n");
		printf("  remove           Tell other nodes to ajust quorum downwards if necessary\n");
		printf("  force            Leave even if cluster subsystems are active\n");
		printf("\n");
	}

	if (!subcmd || subcmd == OP_KILL) {
		printf("kill\n");
		printf("  -n <nodename>    The name of the node to kill (can specify multiple times)\n");
		printf("\n");
	}

	if (!subcmd || subcmd == OP_EXPECTED) {
		printf("expected\n");
		printf("  -e <votes>       New number of expected votes for the cluster\n");
		printf("\n");
	}

	if (!subcmd || subcmd == OP_VOTES) {
		printf("votes\n");
		printf("  -v <votes>       New number of votes for this node\n");
		printf("\n");
	}

	if (!subcmd || subcmd == OP_STATUS) {
		printf("status             Show local record of cluster status\n");
		printf("\n");
	}

	if (!subcmd || subcmd == OP_NODES) {
		printf("nodes              Show local record of cluster nodes\n");
		printf("  -f                 Also show when node was last fenced\n");
		printf("  -a                 Also show node address(es)\n");
		printf("  -n <nodename>      Only show information for specific node\n");
		printf("  -F <format>        Specify output format (see man page)\n");
		printf("\n");
	}

	if (!subcmd || subcmd == OP_SERVICES) {
		printf("services           Show local record of cluster services\n");
		printf("\n");
	}

	if (!subcmd || subcmd == OP_VERSION) {
		printf("version\n");
		printf("  -r <config>      A new config version to set on all members\n");
		printf("\n");
	}
}

static void sigalarm_handler(int sig)
{
	fprintf(stderr, "Timed-out waiting for cluster\n");
	exit(2);
}

static cman_handle_t open_cman_handle(int priv)
{
	cman_handle_t h;

	if (priv)
		h = cman_admin_init(NULL);
	else
		h = cman_init(NULL);
	if (!h)
	{
		if (errno == EACCES)
			die("Cannot open connection to cman, permission denied.");
		else
			die("Cannot open connection to cman, is it running ?");
	}
	return h;
}

static void print_address(char *addr)
{
	char buf[INET6_ADDRSTRLEN];
	struct sockaddr_storage *ss = (struct sockaddr_storage *)addr;
	struct sockaddr_in *sin = (struct sockaddr_in *)addr;
	struct sockaddr_in6 *sin6 = (struct sockaddr_in6 *)addr;
	void *saddr;

	if (ss->ss_family == AF_INET6)
		saddr = &sin6->sin6_addr;
	else
		saddr = &sin->sin_addr;

	inet_ntop(ss->ss_family, saddr, buf, sizeof(buf));
	printf("%s", buf);
}

static char *membership_state(char *buf, int buflen, int node_state)
{
	switch (node_state) {
	case 1:
		strncpy(buf, "Joining", buflen);
		break;
	case 2:
		strncpy(buf, "Cluster-Member", buflen);
		break;
	case 3:
		strncpy(buf, "Not-in-Cluster", buflen);
		break;
	case 4:
		strncpy(buf, "Leaving", buflen);
		break;
	default:
		sprintf(buf, "Unknown: code=%d", node_state);
		break;
	}

	return buf;
}

static void show_status(void)
{
	cman_cluster_t info;
	cman_version_t v;
	cman_handle_t h;
	cman_node_t node;
	char info_buf[PIPE_BUF];
	char tmpbuf[1024];
	cman_extra_info_t *einfo = (cman_extra_info_t *)info_buf;
	cman_qdev_info_t qinfo;
	int quorate;
	int i;
	int j;
	int portnum;
	char *addrptr;

	h = open_cman_handle(0);

	if (cman_get_cluster(h, &info) < 0)
		die("Error getting cluster info: %s\n", cman_error(errno));
	if (cman_get_version(h, &v) < 0)
		die("Error getting cluster version: %s\n", cman_error(errno));
	if (cman_get_extra_info(h, einfo, sizeof(info_buf)) < 0)
		die("Error getting extra info: %s\n", cman_error(errno));

	quorate = cman_is_quorate(h);

	printf("Version: %d.%d.%d\n", v.cv_major, v.cv_minor, v.cv_patch);
	printf("Config Version: %d\n", v.cv_config);
	printf("Cluster Name: %s\n", info.ci_name);
	printf("Cluster Id: %d\n", info.ci_number);
	printf("Cluster Member: Yes\n");
	printf("Cluster Generation: %d\n", info.ci_generation);
	printf("Membership state: %s\n", membership_state(tmpbuf, sizeof(tmpbuf),
							  einfo->ei_node_state));
	printf("Nodes: %d\n", einfo->ei_members);
	printf("Expected votes: %d\n", einfo->ei_expected_votes);
	if (cman_get_quorum_device(h, &qinfo) == 0 && qinfo.qi_state == 2)
		printf("Quorum device votes: %d\n", qinfo.qi_votes);
	printf("Total votes: %d\n", einfo->ei_total_votes);
	printf("Node votes: %d\n", einfo->ei_node_votes);

	printf("Quorum: %d %s\n", einfo->ei_quorum, quorate?" ":"Activity blocked");
	printf("Active subsystems: %d\n", cman_get_subsys_count(h));
	printf("Flags:");
	if (einfo->ei_flags & CMAN_EXTRA_FLAG_2NODE)
		printf(" 2node");
	if (einfo->ei_flags & CMAN_EXTRA_FLAG_SHUTDOWN)
		printf(" Shutdown");
	if (einfo->ei_flags & CMAN_EXTRA_FLAG_ERROR)
		printf(" Error");
	if (einfo->ei_flags & CMAN_EXTRA_FLAG_DISALLOWED)
		printf(" DisallowedNodes");
	if (einfo->ei_flags & CMAN_EXTRA_FLAG_DIRTY)
		printf(" Dirty");
	printf(" \n");

	printf("Ports Bound: ");
	portnum = 0;
	for (i=0; i<32; i++) {
		for (j=0; j<8; j++) {
			if ((einfo->ei_ports[i] >> j) & 1)
				printf("%d ", portnum);
			portnum++;
		}
	}
	printf(" \n");

	node.cn_name[0] = 0;
	if (cman_get_node(h, CMAN_NODEID_US, &node) == 0) {
		printf("Node name: %s\n", node.cn_name);
		printf("Node ID: %d\n", node.cn_nodeid);
	}

	printf("Multicast addresses: ");
	addrptr = einfo->ei_addresses;
	for (i=0; i < einfo->ei_num_addresses; i++) {
		print_address(addrptr);
		printf(" ");
		addrptr += sizeof(struct sockaddr_storage);
	}
	printf("\n");

	printf("Node addresses: ");
	for (i=0; i < einfo->ei_num_addresses; i++) {
		print_address(addrptr);
		printf(" ");
		addrptr += sizeof(struct sockaddr_storage);
	}
	printf("\n");

	if (einfo->ei_flags & CMAN_EXTRA_FLAG_DISALLOWED) {
		int count;
		int numnodes;
		cman_node_t *nodes;

		count = cman_get_node_count(h);
		nodes = malloc(sizeof(cman_node_t) * count);

		if (cman_get_disallowed_nodes(h, count, &numnodes, nodes) == 0) {
			printf("Disallowed nodes: ");
			for (i=0; i<numnodes; i++) {
				printf("%s ", nodes[i].cn_name);
			}
			printf("\n");
		}
	}
	cman_finish(h);
}

static int node_compare(const void *va, const void *vb)
{
	const cman_node_t *a = va;
	const cman_node_t *b = vb;
	return a->cn_nodeid - b->cn_nodeid;
}

static int node_filter(commandline_t *comline, const char *node)
{
	int i;

	for (i = 0; i < comline->num_nodenames; i++) {
		if (strcmp(comline->nodenames[i], node) == 0) {
			return TRUE;
		}
	}

	return FALSE;
}

static int get_format_opt(const char *opt)
{
	if (!opt)
		return FMT_NONE;

	if (!strcmp(opt, "id"))
		return FMT_ID;
	if (!strcmp(opt, "name"))
		return FMT_NAME;
	if (!strcmp(opt, "type"))
		return FMT_TYPE;
	if (!strcmp(opt, "addr"))
		return FMT_ADDR;

	return FMT_NONE;
}


static void print_node(commandline_t *comline, cman_handle_t h, int *format, struct cman_node *node)
{
	char member_type;
	struct tm *ftime;
	struct tm *jtime;
	char jstring[1024];
	int i,j,k;

	if (comline->num_nodenames > 0) {
		if (node_filter(comline, node->cn_name) == 0) {
			return;
		}
	}

	switch (node->cn_member) {
	case 0:
		member_type = 'X';
		break;
	case 1:
		member_type = 'M';
		break;
	case 2:
		member_type = 'd';
		break;
	default:
		member_type = '?';
		break;
	}

	jtime = localtime(&node->cn_jointime.tv_sec);
	if (node->cn_jointime.tv_sec && node->cn_member)
		strftime(jstring, sizeof(jstring), "%F %H:%M:%S", jtime);
	else
		strcpy(jstring, "                   ");

	if (!comline->format_opts) {
		printf("%4d   %c  %5d   %s  %s\n",
		       node->cn_nodeid, member_type,
		       node->cn_incarnation, jstring, node->cn_name);
	}

	if (comline->fence_opt && !comline->format_opts) {
		char agent[255];
		uint64_t fence_time;
		int fenced;

		if (!cman_get_fenceinfo(h, node->cn_nodeid, &fence_time, &fenced, agent)) {
			if (fence_time) {
				time_t fence_time_t = (time_t)fence_time;
				ftime = localtime(&fence_time_t);
				strftime(jstring, sizeof(jstring), "%F %H:%M:%S", ftime);
				printf("       Last fenced:   %-15s by %s\n", jstring, agent);
			}
			if (!node->cn_member && node->cn_incarnation && !fenced) {
				printf("       Node has not been fenced since it went down\n");
			}
		}
	}

	int numaddrs;
	struct cman_node_address addrs[MAX_INTERFACES];

	if (comline->addresses_opt || comline->format_opts) {
		if (!cman_get_node_addrs(h, node->cn_nodeid, MAX_INTERFACES, &numaddrs, addrs) &&
		    numaddrs)
		{
			if (!comline->format_opts) {
				printf("       Addresses: ");
				for (i = 0; i < numaddrs; i++)
				{
					print_address(addrs[i].cna_address);
					printf(" ");
				}
				printf("\n");
			}
		}
	}

	if (comline->format_opts) {
		for (j = 0; j < MAX_FORMAT_OPTS; j++) {
			switch (format[j]) {
			case FMT_NONE:
				break;
			case FMT_ID:
				printf("%d ", node->cn_nodeid);
				break;
			case FMT_NAME:
				printf("%s ", node->cn_name);
				break;
			case FMT_TYPE:
				printf("%c ", member_type);
				break;
			case FMT_ADDR:
				for (k = 0; k < numaddrs; k++) {
					print_address(addrs[k].cna_address);
					if (k != (numaddrs - 1)) {
						printf(",");
					}
				}
				printf(" ");
				break;
			default:
				break;
			}
		}
		printf("\n");
	}
}

static void show_nodes(commandline_t *comline)
{
	cman_handle_t h;
	int count;
	int i;
	int j;
	int numnodes;
	int dis_count;
	int format[MAX_FORMAT_OPTS];
	cman_node_t *dis_nodes;
	cman_node_t *nodes;

	h = open_cman_handle(0);

	count = cman_get_node_count(h);
	if (count < 0)
		die("cman_get_node_count failed: %s", cman_error(errno));

	count += 2; /* Extra space! */

	nodes = malloc(sizeof(cman_node_t) * count);
	if (!nodes)
		die("cannot allocate memory for nodes list\n");

	if (comline->format_opts != NULL) {
		char *format_str = comline->format_opts;
		char *format_tmp;
		for (i = 0; i < MAX_FORMAT_OPTS; i++) {
			format_tmp = strtok(format_str, ",");
			format_str = NULL;
			format[i] = get_format_opt(format_tmp);
		}
	}

	if (cman_get_nodes(h, count, &numnodes, nodes) < 0)
		die("cman_get_nodes failed: %s", cman_error(errno));


	/* Get Disallowed nodes, so we can show them as such */
	dis_nodes = malloc(sizeof(cman_node_t) * count);

	if (cman_get_disallowed_nodes(h, count, &dis_count, dis_nodes) == 0) {
		for (i = 0; i < numnodes; i++) {
			for (j = 0; j < dis_count; j++) {
				if (dis_nodes[j].cn_nodeid == nodes[i].cn_nodeid)
					nodes[i].cn_member = 2;
			}
		}
	}

	/* Sort by nodeid to be friendly */
	qsort(nodes, numnodes, sizeof(cman_node_t), node_compare);

	if (dis_count) {
		printf("NOTE: There are %d disallowed nodes,\n", dis_count);
		printf("      members list may seem inconsistent across the cluster\n");
	}

	if (!comline->format_opts) {
		printf("Node  Sts   Inc   Joined               Name\n");
	}

	/* Print nodes */
	for (i = 0; i < numnodes; i++) {
		print_node(comline, h, format, &nodes[i]);
	}

	free(nodes);
	free(dis_nodes);
	cman_finish(h);
}

static int show_services(void)
{
	return system("group_tool ls");
}


char *cman_error(int err)
{
	char *die_error;

	switch (errno) {
	case ENOTCONN:
		die_error = "Cluster software not started";
		break;
	case ENOENT:
		die_error = "Node is not yet a cluster member";
		break;
	default:
		die_error = strerror(errno);
		break;
	}
	return die_error;
}

static void leave(commandline_t *comline)
{
	cman_handle_t h;
	int result;
	int flags = 0;

	h = open_cman_handle(1);

	/* "cman_tool leave remove" adjusts quorum downward */

	if (comline->remove)
		flags |= CMAN_SHUTDOWN_REMOVED;

	if (comline->force)
		flags |= CMAN_SHUTDOWN_ANYWAY;

	if (comline->wait_opt && comline->timeout) {
		signal(SIGALRM, sigalarm_handler);
		alarm(comline->timeout);
	}

	result = cman_shutdown(h, flags);
	if (result && !comline->wait_opt) {
		die("Error leaving cluster: %s", cman_error(errno));
	}
	cman_finish(h);

	/* Wait until cman shuts down */
	if (comline->wait_opt) {
		while ( (h = cman_admin_init(NULL)) ) {
			cman_finish(h);
			sleep(1);
		}
	}
}

static void set_expected(commandline_t *comline)
{
	cman_handle_t h;
	int result;

	h = open_cman_handle(1);

	if ((result = cman_set_expected_votes(h, comline->expected_votes)))
		die("can't set expected votes: %s", cman_error(errno));

	cman_finish(h);
}

static void set_votes(commandline_t *comline)
{
	cman_handle_t h;
	int result;
	int nodeid;
	struct cman_node node;


	h = open_cman_handle(1);

	if (!comline->num_nodenames) {
		nodeid = 0; /* This node */
	}
	else {
		/* Resolve node name into a number */
		node.cn_nodeid = 0;
		strcpy(node.cn_name, comline->nodenames[0]);
		if (cman_get_node(h, node.cn_nodeid, &node))
			die("Can't set votes for node %s : %s\n", node.cn_name, strerror(errno));
		nodeid = node.cn_nodeid;
	}

	if ((result = cman_set_votes(h, comline->votes, nodeid)))
		die("can't set votes: %s", cman_error(errno));

	cman_finish(h);
}

static void version(commandline_t *comline)
{
	struct cman_version ver;
	cman_handle_t h;
	int result;

	h = open_cman_handle(1);

	if ((result = cman_get_version(h, &ver)))
		die("can't get version: %s", cman_error(errno));

	if (!comline->config_version) {
		printf("%d.%d.%d config %d\n", ver.cv_major, ver.cv_minor, ver.cv_patch,
		       ver.cv_config);
		goto out;
	}

	ver.cv_config = comline->config_version;

	if ((result = cman_set_version(h, &ver)))
		die("can't set version: %s", cman_error(errno));
 out:
	cman_finish(h);
}

static int cluster_wait(commandline_t *comline)
{
	cman_handle_t h;
	int ret = 0;

	h = open_cman_handle(0);

	if (comline->wait_quorate_opt) {
		while (cman_is_quorate(h) <= 0) {
			sleep(1);
		}
	}
	else {
		while (cman_get_node_count(h) < 0) {
			sleep(1);
	    }
	}

	cman_finish(h);

	return ret;
}

static void kill_node(commandline_t *comline)
{
	cman_handle_t h;
	int i;
	struct cman_node node;

	if (!comline->num_nodenames) {
	    die("No node name specified\n");
	}

	h = open_cman_handle(1);

	for (i=0; i<comline->num_nodenames; i++) {

	    /* Resolve node name into a number */
	    node.cn_nodeid = 0;
	    strcpy(node.cn_name, comline->nodenames[i]);
	    if (cman_get_node(h, node.cn_nodeid, &node)) {
		fprintf(stderr, "Can't kill node %s : %s\n", node.cn_name, strerror(errno));
		continue;
	    }


	    if (cman_kill_node(h, node.cn_nodeid))
		perror("kill node failed");
	}

	cman_finish(h);
}

static void set_debuglog(commandline_t *comline)
{
	cman_handle_t h;

	h = open_cman_handle(1);

	if (cman_set_debuglog(h, comline->verbose))
		perror("setting debuglog failed");

	cman_finish(h);
}

#ifdef DEBUG
static void dump_objdb(commandline_t *comline)
{
	cman_handle_t h;

	h = open_cman_handle(1);

	if (cman_dump_objdb(h, comline->filename))
		perror("dump objdb failed");

	cman_finish(h);
}
#endif

static int get_int_arg(char argopt, char *arg)
{
	char *tmp;
	int val;

	val = strtol(arg, &tmp, 10);
	if (tmp == arg || tmp != arg + strlen(arg))
		die("argument to %c (%s) is not an integer", argopt, arg);

	if (val < 0)
		die("argument to %c cannot be negative", argopt);

	return val;
}


static void decode_arguments(int argc, char *argv[], commandline_t *comline)
{
	int cont = TRUE;
	int optchar, i;
	int show_help = 0;

	comline->config_lcrso=DEFAULT_CONFIG_MODULE;

	while (cont) {
		optchar = getopt(argc, argv, OPTION_STRING);

		switch (optchar) {

		case 'm':
			comline->multicast_addr = strdup(optarg);
			break;

		case 'f':
			comline->fence_opt = 1;
			break;

		case 'a':
			comline->addresses_opt = 1;
			break;

		case 'n':
		        i = comline->num_nodenames;
			if (i >= MAX_INTERFACES)
			        die("maximum of %d node names allowed",
				    MAX_INTERFACES);
			if (strlen(optarg) > MAX_NODE_NAME_LEN)
				die("maximum node name length is %d",
				    MAX_NODE_NAME_LEN);
			comline->nodenames[i] = strdup(optarg);
			comline->num_nodenames++;
			break;

		case 'o':
			comline->override_nodename = strdup(optarg);
			break;

		case 'k':
			comline->key_filename = strdup(optarg);
			break;

		case 'C':
			comline->config_lcrso = strdup(optarg);
			break;

		case 'r':
			comline->config_version = get_int_arg(optchar, optarg);
			comline->config_version_opt = TRUE;
			break;

		case 'v':
			comline->votes = get_int_arg(optchar, optarg);
			comline->votes_opt = TRUE;
			break;

		case 'e':
			comline->expected_votes = get_int_arg(optchar, optarg);
			comline->expected_votes_opt = TRUE;
			break;

		case '2':
			comline->two_node = TRUE;
			break;

		case 'p':
			comline->port = get_int_arg(optchar, optarg);
			comline->port_opt = TRUE;
			break;

		case 'N':
			comline->nodeid = get_int_arg(optchar, optarg);
			comline->nodeid_opt = TRUE;
			break;

		case 'c':
			if (strlen(optarg) > MAX_NODE_NAME_LEN-1)
				die("maximum cluster name length is %d",
				    MAX_CLUSTER_NAME_LEN-1);
			strcpy(comline->clustername, optarg);
			comline->clustername_opt = TRUE;
			break;

		case 'F':
			comline->format_opts = strdup(optarg);
			break;

		case 'V':
			printf("cman_tool %s (built %s %s)\n",
				RELEASE_VERSION, __DATE__, __TIME__);
			printf("%s\n", REDHAT_COPYRIGHT);
			exit(EXIT_SUCCESS);
			break;

		case 'h':
			show_help = 1;
			break;

		case ':':
		case '?':
			fprintf(stderr, "Please use '-h' for usage.\n");
			exit(EXIT_FAILURE);
			break;

		case 'd':
			if (optarg)
				comline->verbose = atoi(optarg);
			else
				comline->verbose = 255;
			break;

		case 'w':
			comline->wait_opt = TRUE;
			break;

		case 'q':
			comline->wait_quorate_opt = TRUE;
			break;

		case 't':
			comline->timeout = get_int_arg(optchar, optarg);
			break;

		case EOF:
			cont = FALSE;
			break;

		case 'X':
			comline->noconfig_opt = TRUE;
			break;

		case 'A':
			comline->noopenais_opt = TRUE;
			break;

		case 'P':
			comline->nosetpri_opt = TRUE;
			break;
		default:
			die("unknown option: %c", optchar);
			break;
		};
	}

	while (optind < argc) {
		if (strcmp(argv[optind], "join") == 0) {
			if (comline->operation)
				die("can't specify two operations");
			comline->operation = OP_JOIN;
		} else if (strcmp(argv[optind], "leave") == 0) {
			if (comline->operation)
				die("can't specify two operations");
			comline->operation = OP_LEAVE;
		} else if (strcmp(argv[optind], "expected") == 0) {
			if (comline->operation)
				die("can't specify two operations");
			comline->operation = OP_EXPECTED;
		} else if (strcmp(argv[optind], "votes") == 0) {
			if (comline->operation)
				die("can't specify two operations");
			comline->operation = OP_VOTES;
		} else if (strcmp(argv[optind], "kill") == 0) {
			if (comline->operation)
				die("can't specify two operations");
			comline->operation = OP_KILL;
		} else if (strcmp(argv[optind], "version") == 0) {
			if (comline->operation)
				die("can't specify two operations");
			comline->operation = OP_VERSION;
		} else if (strcmp(argv[optind], "wait") == 0) {
			if (comline->operation)
				die("can't specify two operations");
			comline->operation = OP_WAIT;
		} else if (strcmp(argv[optind], "status") == 0) {
			if (comline->operation)
				die("can't specify two operations");
			comline->operation = OP_STATUS;
		} else if (strcmp(argv[optind], "nodes") == 0) {
			if (comline->operation)
				die("can't specify two operations");
			comline->operation = OP_NODES;
		} else if (strcmp(argv[optind], "services") == 0) {
			if (comline->operation)
				die("can't specify two operations");
			comline->operation = OP_SERVICES;
		} else if (strcmp(argv[optind], "debug") == 0) {
			if (comline->operation)
				die("can't specify two operations");
			comline->operation = OP_DEBUG;
#ifdef DEBUG
		} else if (strcmp(argv[optind], "dump-db") == 0) {
			if (comline->operation)
				die("can't specify two operations");
			comline->operation = OP_DUMP_OBJDB;
			if (!argv[optind+1])
				die("no filename given");
			comline->filename = strdup(argv[optind+1]);
			if (comline->filename[0] != '/')
				die("dump filename must be an absolute path");
			optind++;
#endif
		} else if (strcmp(argv[optind], "remove") == 0) {
			comline->remove = TRUE;
		} else if (strcmp(argv[optind], "force") == 0) {
			comline->force = TRUE;
		} else
			die("unknown option %s", argv[optind]);

		optind++;
	}

	if (show_help) {
		print_usage(comline->operation);
		exit(EXIT_SUCCESS);
	}

	if (!comline->operation)
		die("no operation specified");
}

static void check_arguments(commandline_t *comline)
{
	if (comline->two_node && comline->expected_votes != 1)
		die("expected_votes value (%d) invalid in two node mode",
		    comline->expected_votes);

	if (comline->port_opt &&
	    (comline->port <= 0 || comline->port > 65535))
		die("Port must be a number between 1 and 65535");

	/* This message looks like it contradicts the condition but
	   a nodeid of zero simply means "assign one for me" and is a
	   perfectly reasonable override */
	if (comline->nodeid < 0 || comline->nodeid > 4096)
	        die("Node id must be between 1 and 4096");

	if (strlen(comline->clustername) > MAX_CLUSTER_NAME_LEN) {
	        die("Cluster name must be < %d characters long",
		    MAX_CLUSTER_NAME_LEN);
	}

	if (comline->timeout && !comline->wait_opt && !comline->wait_quorate_opt)
		die("timeout is only appropriate with wait");
}

int main(int argc, char *argv[], char *envp[])
{
	commandline_t comline;
	int ret;

	prog_name = argv[0];

	memset(&comline, 0, sizeof(commandline_t));

	decode_arguments(argc, argv, &comline);

	switch (comline.operation) {
	case OP_JOIN:
		check_arguments(&comline);

		if (comline.timeout) {
			signal(SIGALRM, sigalarm_handler);
			alarm(comline.timeout);
		}

		join(&comline, envp);
		if (comline.wait_opt || comline.wait_quorate_opt) {
			do {
				ret = cluster_wait(&comline);
				if (ret == ENOTCONN)
					join(&comline, envp);

			} while (ret == ENOTCONN);
		}
		break;

	case OP_LEAVE:
		leave(&comline);
		break;

	case OP_EXPECTED:
		set_expected(&comline);
		break;

	case OP_VOTES:
		set_votes(&comline);
		break;

	case OP_KILL:
		kill_node(&comline);
		break;

	case OP_VERSION:
		version(&comline);
		break;

	case OP_WAIT:
		if (comline.timeout) {
			signal(SIGALRM, sigalarm_handler);
			alarm(comline.timeout);
		}
		cluster_wait(&comline);
		break;

	case OP_STATUS:
		show_status();
		break;

	case OP_NODES:
		show_nodes(&comline);
		break;

	case OP_SERVICES:
		if (show_services() < 0) {
			fprintf(stderr, "Unable to invoke group_tool\n");
			exit(EXIT_FAILURE);
		}
		break;

	case OP_DEBUG:
		set_debuglog(&comline);
		break;
#ifdef DEBUG
	case OP_DUMP_OBJDB:
		dump_objdb(&comline);
		break;
#endif
	}

	exit(EXIT_SUCCESS);
}

char *prog_name;
