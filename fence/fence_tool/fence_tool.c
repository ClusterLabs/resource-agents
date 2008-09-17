#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <signal.h>
#include <string.h>
#include <stdint.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <fcntl.h>
#include <limits.h>
#include <errno.h>
#include <libgen.h>

#include "ccs.h"
#include "copyright.cf"
#include "libcman.h"
#include "libfenced.h"

#define OP_JOIN  			1
#define OP_LEAVE 			2
#define OP_LIST				3
#define OP_DUMP				4

#define DEFAULT_WAIT_TIMEOUT		300 /* five minutes */

#define MAX_NODES			128

int all_nodeids[MAX_NODES];
int all_nodeids_count;
cman_node_t cman_nodes[MAX_NODES];
int cman_nodes_count;
struct fenced_node nodes[MAX_NODES];
char *prog_name;
int operation;
int verbose = 0;
int inquorate_fail = 0;
int wait_join = 0;			 /* default: don't wait for join */
int wait_leave = 0;			 /* default: don't wait for leave */
int wait_members = 0;			 /* default: don't wait for members */
int wait_timeout = DEFAULT_WAIT_TIMEOUT;

#define die(fmt, args...) \
do { \
	fprintf(stderr, "%s: ", prog_name); \
	fprintf(stderr, fmt "\n", ##args); \
	exit(EXIT_FAILURE); \
} while (0)

static int do_write(int fd, void *buf, size_t count)
{
	int rv, off = 0;

 retry:
	rv = write(fd, buf + off, count);
	if (rv == -1 && errno == EINTR)
		goto retry;
	if (rv < 0)
		return rv;

	if (rv != count) {
		count -= rv;
		off += rv;
		goto retry;
	}
	return 0;
}

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

static int check_mounted(void)
{
	FILE *file;
	char line[PATH_MAX];
	char device[PATH_MAX];
	char path[PATH_MAX];
	char type[PATH_MAX];

	file = fopen("/proc/mounts", "r");
	if (!file)
		return 0;

	while (fgets(line, PATH_MAX, file)) {
		if (sscanf(line, "%s %s %s", device, path, type) != 3)
			continue;
		if (!strcmp(type, "gfs") || !strcmp(type, "gfs2"))
			die("cannot leave, %s file system mounted from %s on %s",
			    type, device, path);
	}

	fclose(file);
	return 0;
}

static int we_are_in_fence_domain(void)
{
	struct fenced_node nodeinfo;
	int rv;

	memset(&nodeinfo, 0, sizeof(nodeinfo));

	rv = fenced_node_info(FENCED_NODEID_US, &nodeinfo);
	if (rv < 0)
		return 0;

	if (nodeinfo.member)
		return 1;
	return 0;
}

static void wait_domain(int joining)
{
	int in, tries = 0;

	while (1) {
		in = we_are_in_fence_domain();

		if (joining && in)
			break;

		if (!joining && !in)
			break;

		if (tries++ >= wait_timeout)
			goto fail;

		if (!(tries % 5))
			printf("Waiting for fenced to %s the fence group.\n",
			       joining ? "join" : "leave");

		sleep(1);
	}

	return;
 fail:
	printf("Error %s the fence group.\n", joining ? "joining" : "leaving");
}

static void read_ccs_nodeids(int cd)
{
	char path[PATH_MAX];
	char *nodeid_str;
	int i, error;

	memset(all_nodeids, 0, sizeof(all_nodeids));
	all_nodeids_count = 0;

	for (i = 1; ; i++) {
		nodeid_str = NULL;
		memset(path, 0, sizeof(path));
		sprintf(path, "/cluster/clusternodes/clusternode[%d]/@nodeid", i);

		error = ccs_get(cd, path, &nodeid_str);
		if (error || !nodeid_str)
			break;

		all_nodeids[all_nodeids_count++] = atoi(nodeid_str);
		free(nodeid_str);
	}
}

static int all_nodeids_are_members(cman_handle_t ch)
{
	int i, j, rv, found;

	memset(&cman_nodes, 0, sizeof(cman_nodes));
	cman_nodes_count = 0;

	rv = cman_get_nodes(ch, MAX_NODES, &cman_nodes_count, cman_nodes);
	if (rv < 0) {
		printf("cman_get_nodes error %d %d\n", rv, errno);
		return 0;
	}

	for (i = 0; i < all_nodeids_count; i++) {
		found = 0;

		for (j = 0; j < cman_nodes_count; j++) {
			if (cman_nodes[j].cn_nodeid == all_nodeids[i] &&
			    cman_nodes[j].cn_member) {
				found = 1;
				break;
			}
		}

		if (!found)
			return 0;
	}
	return 1;
}

static void wait_cman(void)
{
	cman_handle_t ch;
	int try_init = 0, try_active = 0, try_quorate = 0;
	int try_ccs = 0, try_members = 0;
	int rv, cd;

	while (1) {
		ch = cman_init(NULL);
		if (ch)
			break;

		if (inquorate_fail)
			goto fail;

		if (try_init++ >= wait_timeout) {
			printf("%s: timed out waiting for cman init\n",
			       prog_name);
			goto fail;
		}

		if (!(try_init % 10))
			printf("%s: waiting for cman to start\n", prog_name);

		sleep(1);
	}

	while (1) {
		rv = cman_is_active(ch);
		if (rv)
			break;

		if (inquorate_fail)
			goto fail;

		if (try_active++ >= wait_timeout) {
			printf("%s: timed out waiting for cman active\n",
			       prog_name);
			goto fail;
		}

		if (!(try_active % 10))
			printf("%s: waiting for cman active\n", prog_name);
		sleep(1);
	}

	while (1) {
		rv = cman_is_quorate(ch);
		if (rv)
			break;

		if (inquorate_fail)
			goto fail;

		if (try_quorate++ >= wait_timeout) {
			printf("%s: timed out waiting for cman quorum\n",
			       prog_name);
			goto fail;
		}

		if (!(try_quorate % 10))
			printf("%s: waiting for cman quorum\n", prog_name);

		sleep(1);
	}

	while (1) {
		cd = ccs_connect();
		if (cd > 0)
			break;

		if (try_ccs++ >= wait_timeout) {
			printf("%s: timed out waiting for ccs connect\n",
			       prog_name);
			goto fail;
		}

		if (!(try_ccs % 10))
			printf("%s: waiting for ccs connect\n", prog_name);

		sleep(1);
	}

	if (!wait_members)
		goto out;
	read_ccs_nodeids(cd);

	while (1) {
		rv = all_nodeids_are_members(ch);
		if (rv)
			break;

		if (try_members++ >= wait_members)
			break;

		if (!(try_members % 10))
			printf("%s: waiting for all %d nodes to be members\n",
			       prog_name, all_nodeids_count);
		sleep(1);
	}

 out:
	ccs_disconnect(cd);
	cman_finish(ch);
	return;

 fail:
	if (ch)
		cman_finish(ch);
	exit(EXIT_FAILURE);
}

static void do_join(int argc, char *argv[])
{
	int rv;

	wait_cman();

	rv = fenced_join();
	if (rv < 0)
		die("can't communicate with fenced");

	if (wait_join)
		wait_domain(1);

	exit(EXIT_SUCCESS);
}

static void do_leave(void)
{
	int rv;

	check_mounted();

	rv = fenced_leave();
	if (rv < 0)
		die("can't communicate with fenced");

	if (wait_leave)
		wait_domain(0);

	exit(EXIT_SUCCESS);
}

static void do_dump(void)
{
	char buf[FENCED_DUMP_SIZE];
	int rv;

	rv = fenced_dump_debug(buf);
	if (rv < 0)
		die("can't communicate with fenced");

	do_write(STDOUT_FILENO, buf, sizeof(buf));

	exit(EXIT_SUCCESS);
}

static int node_compare(const void *va, const void *vb)
{
	const struct fenced_node *a = va;
	const struct fenced_node *b = vb;

	return a->nodeid - b->nodeid;
}

static int do_list(void)
{
	struct fenced_domain d;
	struct fenced_node *np;
	int node_count;
	int rv, i;

	rv = fenced_domain_info(&d);
	if (rv < 0)
		goto fail;

	printf("fence domain\n");
	printf("member count  %d\n", d.member_count);
	printf("victim count  %d\n", d.victim_count);
	printf("victim now    %d\n", d.current_victim);
	printf("master nodeid %d\n", d.master_nodeid);
	printf("members       ");

	node_count = 0;
	memset(&nodes, 0, sizeof(nodes));

	rv = fenced_domain_nodes(FENCED_NODES_MEMBERS, MAX_NODES,
				 &node_count, nodes);
	if (rv < 0) {
		printf("error\n");
		goto fail;
	}

	qsort(&nodes, node_count, sizeof(struct fenced_node), node_compare);

	np = nodes;
	for (i = 0; i < node_count; i++) {
		printf("%d ", np->nodeid);
		np++;
	}
	printf("\n");

	if (!verbose) {
		printf("\n");
		exit(EXIT_SUCCESS);
	}

	node_count = 0;
	memset(&nodes, 0, sizeof(nodes));

	rv = fenced_domain_nodes(FENCED_NODES_ALL, MAX_NODES,
				 &node_count, nodes);
	if (rv < 0)
		goto fail;

	qsort(&nodes, node_count, sizeof(struct fenced_node), node_compare);

	printf("all nodes\n");

	np = nodes;
	for (i = 0; i < node_count; i++) {
		printf("nodeid %d member %d victim %d last fence master %d how %d\n",
				np->nodeid,
				np->member,
				np->victim,
				np->last_fenced_master,
				np->last_fenced_how);
		np++;
	}
	printf("\n");
	exit(EXIT_SUCCESS);
 fail:
	fprintf(stderr, "fenced query error %d\n", rv);
	printf("\n");
	exit(EXIT_FAILURE);
}

static void print_usage(void)
{
	printf("Usage:\n");
	printf("\n");
	printf("%s <join|leave|dump> [options]\n", prog_name);
	printf("\n");
	printf("Actions:\n");
	printf("  join             Join the default fence domain\n");
	printf("  leave            Leave default fence domain\n");
	printf("  ls		   List nodes status\n");
	printf("  dump		   Dump debug buffer from fenced\n");
	printf("\n");
	printf("Options:\n");
	printf("  -m <seconds>     Delay join up to <seconds> for all nodes in cluster.conf\n");
	printf("                   to be cluster members\n");
	printf("  -w               Wait for join or leave to complete\n");
	printf("  -t <seconds>     Maximum time in seconds to wait (default %d)\n", DEFAULT_WAIT_TIMEOUT);
	printf("  -Q               Fail if cluster is not quorate, don't wait\n");
	printf("  -V               Print program version information, then exit\n");
	printf("  -h               Print this help, then exit\n");
	printf("\n");
}

#define OPTION_STRING "vVht:wQm:"

static void decode_arguments(int argc, char *argv[])
{
	int cont = 1;
	int optchar;

	while (cont) {
		optchar = getopt(argc, argv, OPTION_STRING);

		switch (optchar) {

		case 'V':
			printf("fence_tool %s (built %s %s)\n",
			       RELEASE_VERSION, __DATE__, __TIME__);
			printf("%s\n", REDHAT_COPYRIGHT);
			exit(EXIT_SUCCESS);
			break;

		case 'v':
			verbose++;
			break;

		case 'h':
			print_usage();
			exit(EXIT_SUCCESS);
			break;

		case 'Q':
			inquorate_fail = 1;
			break;

		case 'w':
			wait_join = 1;
			wait_leave = 1;
			break;

		case 'm':
			wait_members = atoi(optarg);
			break;

		case 't':
			wait_timeout = get_int_arg(optchar, optarg);
			break;

		case ':':
		case '?':
			fprintf(stderr, "Please use '-h' for usage.\n");
			exit(EXIT_FAILURE);
			break;

		case EOF:
			cont = 0;
			break;

		default:
			die("unknown option: %c\n", optchar);
			break;
		}
	}

	while (optind < argc) {
		if (strcmp(argv[optind], "join") == 0) {
			operation = OP_JOIN;
		} else if (strcmp(argv[optind], "leave") == 0) {
			operation = OP_LEAVE;
		} else if (strcmp(argv[optind], "dump") == 0) {
			operation = OP_DUMP;
		} else if (strcmp(argv[optind], "ls") == 0) {
			operation = OP_LIST;
		} else
			die("unknown option %s\n", argv[optind]);
		optind++;
	}

	if (!operation)
		die("no operation specified\n");
}

int main(int argc, char *argv[])
{
	prog_name = basename(argv[0]);

	decode_arguments(argc, argv);

	switch (operation) {
	case OP_JOIN:
		do_join(argc, argv);
	case OP_LEAVE:
		do_leave();
	case OP_DUMP:
		do_dump();
	case OP_LIST:
		do_list();
	}

	return EXIT_FAILURE;
}

