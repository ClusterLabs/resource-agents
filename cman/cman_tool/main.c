/******************************************************************************
*******************************************************************************
**
**  Copyright (C) Sistina Software, Inc.  1997-2003  All rights reserved.
**  Copyright (C) 2004 Red Hat, Inc.  All rights reserved.
**
**  This copyrighted material is made available to anyone wishing to use,
**  modify, copy, or redistribute it subject to the terms and conditions
**  of the GNU General Public License v.2.
**
*******************************************************************************
******************************************************************************/

#include <inttypes.h>
#include "copyright.cf"
#include "cnxman-socket.h"
#include "cman_tool.h"

#define OPTION_STRING		("m:n:v:e:2p:c:r:i:N:XVh?d")
#define OP_JOIN			1
#define OP_LEAVE		2
#define OP_EXPECTED		3
#define OP_VOTES		4
#define OP_KILL			5
#define OP_VERSION		6


static void print_usage(void)
{
	printf("Usage:\n");
	printf("\n");
	printf("%s <join|leave|kill|expected|votes|version> [options]\n",
	       prog_name);
	printf("\n");
	printf("Options:\n");
	printf("  -h               Print this help, then exit\n");
	printf("  -V               Print program version information, then exit\n");
	printf("  -d               Enable debug output\n");
	printf("\n");

	printf("join\n");
	printf("  -m <addr>      * Multicast address to use (combines with -i)\n");
	printf("  -i <ifname>    * Interfaces for above multicast addresses\n");
	printf("  -v <votes>       Number of votes this node has (default 1)\n");
	printf("  -e <votes>       Number of expected votes for the cluster (no default)\n");
	printf("  -c <clustername> Name of the cluster to join\n");
	printf("  -2               This is a two node cluster (-e must be 1)\n");
	printf("  -p <port>        UDP port number for cman communications (default %d)\n", DEFAULT_PORT);
	printf("  -n <nodename>  * The name of this node (defaults to unqualified hostname)\n");
	printf("  -N <id>          Node id (defaults to automatic)\n");
	printf("  -X               Do not use cluster.conf values from CCS\n");
	printf("  options with marked * can be specified multiple times for multi-path systems\n");

	printf("\n");
	printf("leave\n");
	printf("  remove           Tell other nodes to ajust quorum downwards if necessary\n");
	printf("  force            Leave even if cluster subsystems are active\n");

	printf("\n");
	printf("kill\n");
	printf("  -n <nodename>    The name of the node to kill (can specify multiple times)\n");

	printf("\n");
	printf("expected\n");
	printf("  -e <votes>       New number of expected votes for the cluster\n");

	printf("\n");
	printf("votes\n");
	printf("  -v <votes>       New number of votes for this node\n");

	printf("\n");
	printf("version\n");
	printf("  -r <config>      A new config version to set on all members\n");
	printf("\n");
}

static void leave(commandline_t *comline)
{
	int cluster_sock;
	int result;
	int flags = CLUSTER_LEAVEFLAG_DOWN;

	cluster_sock = socket(AF_CLUSTER, SOCK_DGRAM, CLPROTO_MASTER);
	if (cluster_sock == -1)
		die("can't open cluster socket");

	/* "cman_tool leave remove" adjusts quorum downward */

	if (comline->remove)
		flags |= CLUSTER_LEAVEFLAG_REMOVED;

	/* If the join count is != 1 then there are other things using
	   the cluster and we need to be forced */

	if ((result = ioctl(cluster_sock, SIOCCLUSTER_GET_JOINCOUNT, 0)) != 0) {
		if (result < 0)
			die("error getting join count");

		if (!comline->force) {
	    		die("Can't leave cluster while there are %d active subsystems\n", result);
		}
		flags |= CLUSTER_LEAVEFLAG_FORCE;
	}

	if ((result = ioctl(cluster_sock, SIOCCLUSTER_LEAVE_CLUSTER,
			    flags)))
		die("error leaving cluster");

	close(cluster_sock);
}

static void set_expected(commandline_t *comline)
{
	int cluster_sock;
	int result;

	cluster_sock = socket(AF_CLUSTER, SOCK_DGRAM, CLPROTO_MASTER);
	if (cluster_sock == -1)
		die("Can't open cluster socket");

	if ((result = ioctl(cluster_sock, SIOCCLUSTER_SETEXPECTED_VOTES,
			    comline->expected_votes)))
		die("can't set expected votes");

	close(cluster_sock);
}

static void set_votes(commandline_t *comline)
{
	int cluster_sock;
	int result;

	cluster_sock = socket(AF_CLUSTER, SOCK_DGRAM, CLPROTO_MASTER);
	if (cluster_sock == -1)
		die("can't open cluster socket");

	if ((result = ioctl(cluster_sock, SIOCCLUSTER_SET_VOTES,
			    comline->votes)))
		die("can't set votes");

	close(cluster_sock);
}

static void version(commandline_t *comline)
{
	struct cl_version ver;
	int cluster_sock;
	int result;

	cluster_sock = socket(AF_CLUSTER, SOCK_DGRAM, CLPROTO_MASTER);
	if (cluster_sock == -1)
		die("can't open cluster socket");

	if ((result = ioctl(cluster_sock, SIOCCLUSTER_GET_VERSION, &ver)))
		die("can't get version");

	if (!comline->config_version) {
		printf("%d.%d.%d config %d\n", ver.major, ver.minor, ver.patch,
		       ver.config);
		goto out;
	}

	ver.config = comline->config_version;

	if ((result = ioctl(cluster_sock, SIOCCLUSTER_SET_VERSION, &ver)))
		die("can't set version");
 out:
	close(cluster_sock);
}

static void kill_node(commandline_t *comline)
{
	int cluster_sock;
	int i;
	struct cl_cluster_node node;

	if (!comline->num_nodenames) {
	    die("No node name specified\n");
	}

	cluster_sock = socket(AF_CLUSTER, SOCK_DGRAM, CLPROTO_MASTER);
	if (cluster_sock == -1)
		die("can't open cluster socket");


	for (i=0; i<comline->num_nodenames; i++) {

	    /* Resolve node name into a number */
	    node.node_id = 0;
	    strcpy(node.name, comline->nodenames[i]);
	    if (ioctl(cluster_sock, SIOCCLUSTER_GETNODE, &node)) {
		fprintf(stderr, "Can't kill node %s : %s\n", node.name, strerror(errno));
		continue;
	    }


	    if (ioctl(cluster_sock, SIOCCLUSTER_KILLNODE, node.node_id))
		perror("kill node failed");
	}

	close(cluster_sock);
}

static void decode_arguments(int argc, char *argv[], commandline_t *comline)
{
	int cont = TRUE;
	int optchar, i;

	while (cont) {
		optchar = getopt(argc, argv, OPTION_STRING);

		switch (optchar) {

		case 'i':
			i = comline->num_interfaces;
			if (i >= MAX_INTERFACES)
				die("maximum of %d interfaces allowed",
				    MAX_INTERFACES);
			comline->interfaces[i] = strdup(optarg);
			if (!comline->interfaces[i])
				die("no memory");
			comline->num_interfaces++;
			break;

		case 'm':
		        i = comline->num_multicasts;
			if (i >= MAX_INTERFACES)
			        die("maximum of %d multicast addresses allowed",
				    MAX_INTERFACES);
			if (strlen(optarg) > MAX_MCAST_NAME_LEN)
				die("maximum multicast name length is %d",
				    MAX_MCAST_NAME_LEN);
			comline->multicast_names[i] = strdup(optarg);
			comline->num_multicasts++;
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

		case 'r':
			comline->config_version = atoi(optarg);
			comline->config_version_opt = TRUE;
			break;

		case 'v':
			comline->votes = atoi(optarg);
			comline->votes_opt = TRUE;
			break;

		case 'e':
			comline->expected_votes = atoi(optarg);
			comline->expected_votes_opt = TRUE;
			break;

		case '2':
			comline->two_node = TRUE;
			break;

		case 'p':
			comline->port = atoi(optarg);
			comline->port_opt = TRUE;
			break;

		case 'N':
			comline->nodeid = atoi(optarg);
			comline->nodeid_opt = TRUE;
			break;

		case 'c':
			if (strlen(optarg) > MAX_NODE_NAME_LEN)
				die("maximum cluster name length is %d",
				    MAX_CLUSTER_NAME_LEN);
			strcpy(comline->clustername, optarg);
			comline->clustername_opt = TRUE;
			break;

		case 'X':
			comline->no_ccs = TRUE;
			break;

		case 'V':
			printf("cman_tool %s (built %s %s)\n",
				CMAN_RELEASE_NAME, __DATE__, __TIME__);
			printf("%s\n", REDHAT_COPYRIGHT);
			exit(EXIT_SUCCESS);
			break;

		case 'h':
			print_usage();
			exit(EXIT_SUCCESS);
			break;

		case ':':
		case '?':
			fprintf(stderr, "Please use '-h' for usage.\n");
			exit(EXIT_FAILURE);
			break;

		case 'd':
		        comline->verbose++;
			break;

		case EOF:
			cont = FALSE;
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
		} else if (strcmp(argv[optind], "remove") == 0) {
			comline->remove = TRUE;
		} else if (strcmp(argv[optind], "force") == 0) {
			comline->force = TRUE;
		} else
			die("unknown option %s", argv[optind]);

		optind++;
	}

	if (!comline->operation)
		die("no operation specified");
}

static void check_arguments(commandline_t *comline)
{
	int error;

	if (!comline->expected_votes)
	        die("expected votes not set");

	if (!comline->clustername[0])
		die("cluster name not set");

	if (!comline->votes)
		comline->votes = DEFAULT_VOTES;

	if (!comline->port)
		comline->port = DEFAULT_PORT;

	if (comline->two_node && comline->expected_votes != 1)
		die("expected_votes value (%d) invalid in two node mode",
		    comline->expected_votes);

	if (!comline->nodenames[0]) {
		struct utsname utsname;
		error = uname(&utsname);
		if (error)
			die("cannot get node name, uname failed");

		comline->nodenames[0] = strdup(utsname.nodename);
		comline->num_nodenames++;
	}

	if (!comline->num_interfaces) {
	        comline->interfaces[0] = strdup("eth0");
		if (!comline->interfaces[0])
			die("no memory");
	}

	if (comline->num_multicasts != comline->num_interfaces) {
	        die("Number of multicast addresses (%d) must match number of "
		    "interfaces (%d)", comline->num_multicasts,
		    comline->num_interfaces);
	}

	if (comline->num_nodenames && comline->num_multicasts &&
	    comline->num_nodenames != comline->num_multicasts) {
	        die("Number of node names (%d) must match number of multicast "
		    "addresses (%d)", comline->num_nodenames,
		    comline->num_multicasts);
	}

	if (comline->port <= 0 || comline->port > 65535)
		die("Port must be a number between 1 and 65535");

	/* This message looks like it contradicts the condition but
	   a nodeid of zero simply means "assign one for me" and is a
	   perfectly reasonable override */
	if (comline->nodeid < 0 || comline->nodeid > 4096)
	        die("Node id must be between 1 and 4096");

	if (strlen(comline->clustername) > MAX_CLUSTER_NAME_LEN) {
	        die("Cluster name must be <= %d characters long",
		    MAX_CLUSTER_NAME_LEN);
	}
}

int main(int argc, char *argv[])
{
	commandline_t comline;

	prog_name = argv[0];

	memset(&comline, 0, sizeof(commandline_t));

	decode_arguments(argc, argv, &comline);

	switch (comline.operation) {
	case OP_JOIN:
		if (!comline.no_ccs)
			get_ccs_join_info(&comline);
		check_arguments(&comline);
		join(&comline);
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

	/* FIXME: support CLU_SET_NODENAME? */
	}

	exit(EXIT_SUCCESS);
}

char *prog_name;
