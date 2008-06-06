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

#define die(fmt, args...) \
do \
{ \
  fprintf(stderr, "%s: ", prog_name); \
  fprintf(stderr, fmt "\n", ##args); \
  exit(EXIT_FAILURE); \
} \
while (0)

char *prog_name;
int operation;
int verbose = 0;
int child_wait = 0;
int quorum_wait = 1;
int fenced_start_timeout = 300; /* five minutes */
int signalled = 0;
cman_handle_t ch;

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

#if 0
static int do_read(int fd, void *buf, size_t count)
{
	int rv, off = 0;

	while (off < count) {
		rv = read(fd, buf + off, count - off);
		if (rv == 0)
			return -1;
		if (rv == -1 && errno == EINTR)
			continue;
		if (rv == -1)
			return -1;
		off += rv;
	}
	return 0;
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

static void sigalarm_handler(int sig)
{
	signalled = 1;
}

static int we_are_in_fence_domain(void)
{
#if 0
	group_data_t gdata;
	int rv;

	memset(&gdata, 0, sizeof(gdata));
	rv = group_get_group(0, "default", &gdata);

	if (rv || strcmp(gdata.client_name, "fence"))
		return 0;

	return gdata.member;
#endif
	printf("FIXME: use libfenced:fenced_domain_members()\n");
	return 1;
}

/*
 * We wait for the cluster to be quorate in this program because it's easy to
 * kill this program if we want to quit waiting.  If we just started fenced
 * without waiting for quorum, fenced's join would then wait for quorum in SM
 * but we can't kill/cancel it at that point -- we have to wait for it to
 * complete.
 *
 * A second reason to wait for quorum is that the unfencing step involves
 * cluster.conf lookups through ccs, but ccsd may wait for the cluster to be
 * quorate before responding to the lookups.  There wouldn't be a problem
 * blocking there per se, but it's cleaner I think to just wait here first.
 *
 * In the case where we're leaving, we want to wait for quorum because if we go
 * ahead and shut down fenced, the fence domain leave will block in SM where it
 * will wait for quorum before the leave can be processed.  We can't
 * kill/cancel the leave at that point, but we can if we're waiting here.
 *
 * Waiting here doesn't guarantee we won't end up blocking in SM on the join or
 * leave, but it avoids it in some common cases which can be helpful.  (Quorum
 * could easily be lost between the time we wait for it here and then begin the
 * join/leave process.)
 */

static int check_quorum(void)
{
	int rv = 0, i = 0;

	while (!signalled) {
		rv = cman_is_quorate(ch);
		if (rv)
			return 1;
		else if (!quorum_wait)
			return 0;

		sleep(1);

		if (!signalled && ++i > 9 && !(i % 10))
			printf("%s: waiting for cluster quorum\n", prog_name);
	}

	errno = ETIMEDOUT;
	return 0;
}

static int do_wait(int joining)
{
	int i;

	for (i=0; !fenced_start_timeout || i < fenced_start_timeout; i++) {
		if (we_are_in_fence_domain() == joining)
			return 0;
		if (i && !(i % 5))
			printf("Waiting for fenced to %s the fence group.\n",
				   (joining?"join":"leave"));
		sleep(1);
	}
	printf("Error joining the fence group.\n");
	return -1;
}

static int do_join(int argc, char *argv[])
{
	int rv;

	ch = cman_init(NULL);

	if (fenced_start_timeout) {
		signal(SIGALRM, sigalarm_handler);
		alarm(fenced_start_timeout);
	}

	if (!check_quorum()) {
		if (errno == ETIMEDOUT)
			printf("%s: Timed out waiting for cluster "
			       "quorum to form.\n", prog_name);
		cman_finish(ch);
		return EXIT_FAILURE;
	}
	cman_finish(ch);

	rv = fenced_join();
	if (rv < 0)
		die("can't communicate with fenced");

	if (child_wait)
		do_wait(1);
	return EXIT_SUCCESS;
}

static int do_leave(void)
{
	int rv;

	check_mounted();

	rv = fenced_leave();
	if (rv < 0)
		die("can't communicate with fenced");

	if (child_wait)
		do_wait(0);
	return EXIT_SUCCESS;
}

static int do_dump(void)
{
	char buf[FENCED_DUMP_SIZE];
	int rv;

	rv = fenced_dump_debug(buf);
	if (rv < 0)
		die("can't communicate with fenced");

	do_write(STDOUT_FILENO, buf, sizeof(buf));

	return 0;
}

static int node_compare(const void *va, const void *vb)
{
	const struct fenced_node *a = va;
	const struct fenced_node *b = vb;

	return a->nodeid - b->nodeid;
}

#define MAX_NODES 128

struct fenced_node nodes[MAX_NODES];

static int do_list(void)
{
	struct fenced_domain d;
	struct fenced_node *np;
	int node_count;
	int rv, i;

	rv = fenced_domain_info(&d);
	if (rv < 0)
		goto out;

	printf("fence domain \"default\"\n");
	printf("member_count %d master_nodeid %d victim_count %d current_victim %d state %d\n",
		d.member_count, d.master_nodeid, d.victim_count, d.current_victim, d.state);

	node_count = 0;
	memset(&nodes, 0, sizeof(nodes));

	rv = fenced_domain_nodes(FENCED_NODES_MEMBERS, MAX_NODES,
				 &node_count, nodes);
	if (rv < 0)
		goto out;

	qsort(&nodes, node_count, sizeof(struct fenced_node), node_compare);

	printf("members ");

	np = nodes;
	for (i = 0; i < node_count; i++) {
		printf("%d ", np->nodeid);
		np++;
	}
	printf("\n");

	if (!verbose)
		return 0;

	node_count = 0;
	memset(&nodes, 0, sizeof(nodes));

	rv = fenced_domain_nodes(FENCED_NODES_ALL, MAX_NODES,
				 &node_count, nodes);
	if (rv < 0)
		goto out;

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
	return 0;
 out:
	fprintf(stderr, "fenced query error %d\n", rv);
	return rv;
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
	printf("  dump		   Dump debug buffer from fenced\n");
	printf("\n");
	printf("Options:\n");
	printf("  -w               Wait for join to complete\n");
	printf("  -V               Print program version information, then exit\n");
	printf("  -h               Print this help, then exit\n");
	printf("  -t               Maximum time in seconds to wait\n");
	printf("  -Q               Fail if cluster is not quorate, don't wait\n");
	printf("\n");
}

#define OPTION_STRING "vVht:wQ"

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
			quorum_wait = 0;
			break;

		case 'w':
			child_wait = 1;
			break;

		case ':':
		case '?':
			fprintf(stderr, "Please use '-h' for usage.\n");
			exit(EXIT_FAILURE);
			break;

		case EOF:
			cont = 0;
			break;

		case 't':
			fenced_start_timeout = get_int_arg(optchar, optarg);
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
		return do_join(argc, argv);
	case OP_LEAVE:
		return do_leave();
	case OP_DUMP:
		return do_dump();
	case OP_LIST:
		return do_list();
	}

	return EXIT_FAILURE;
}

