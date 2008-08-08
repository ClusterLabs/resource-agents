#include <sys/types.h>
#include <sys/un.h>
#include <inttypes.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stddef.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include <limits.h>
#include <netinet/in.h>
#include <linux/dlmconstants.h>

#include "libgroup.h"
#include "groupd.h"
#include "libfenced.h"
#include "libdlmcontrol.h"
#include "libgfscontrol.h"
#include "copyright.cf"

#define GROUP_LIBGROUP			2
#define GROUP_LIBCPG			3

#define MAX_NODES			128
#define MAX_LS				128
#define MAX_MG				128
#define MAX_GROUPS			128

#define OP_LIST				1
#define OP_DUMP				2
#define OP_LOG				3

static char *prog_name;
static int operation;
static int opt_ind;
static int verbose;
static int all_daemons;
static int print_header_done;


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

static void print_usage(void)
{
	printf("Usage:\n");
	printf("\n");
	printf("%s [options] [ls|dump]\n", prog_name);
	printf("\n");
	printf("Options:\n");
	printf("  -v               Verbose output, extra event information\n");
	printf("  -h               Print this help, then exit\n");
	printf("  -V               Print program version information, then exit\n");
	printf("\n");
	printf("Display group information from groupd\n");
	printf("ls                 Show information for all groups\n");
	printf("ls <level> <name>  Show information one group\n");
	printf("\n");
	printf("Display debugging information\n");
	printf("dump               Show debug log from groupd\n");
	printf("dump fence         Show debug log from fenced\n");
	printf("dump gfs           Show debug log from gfs_controld\n");
	printf("dump plocks <name> Show posix locks for gfs with given name\n");
	printf("\n");
	printf("log <comments>     Add information to the groupd log.\n");
	printf("\n");
}

#define OPTION_STRING "ahVv"

static void decode_arguments(int argc, char **argv)
{
	int cont = 1;
	int optchar;

	while (cont) {
		optchar = getopt(argc, argv, OPTION_STRING);

		switch (optchar) {
		case 'a':
			all_daemons = 1;
			break;

		case 'v':
			verbose = 1;
			break;

		case 'h':
			print_usage();
			exit(EXIT_SUCCESS);
			break;

		case 'V':
			printf("%s %s (built %s %s)\n",
				prog_name, RELEASE_VERSION, __DATE__, __TIME__);
			printf("%s\n", REDHAT_COPYRIGHT);
			exit(EXIT_SUCCESS);
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
			fprintf(stderr, "unknown option: %c\n", optchar);
			exit(EXIT_FAILURE);
			break;
		};
	}

	while (optind < argc) {
		if (strcmp(argv[optind], "dump") == 0) {
			operation = OP_DUMP;
			opt_ind = optind + 1;
			break;
		} else if (strcmp(argv[optind], "ls") == 0 ||
		           strcmp(argv[optind], "list") == 0) {
			operation = OP_LIST;
			opt_ind = optind + 1;
			break;
		} else if (strcmp(argv[optind], "log") == 0) {
			operation = OP_LOG;
			opt_ind = optind + 1;
			break;
		}
		optind++;
	}

	if (!operation)
		operation = OP_LIST;
}

/* copied from grouip/daemon/gd_internal.h, must keep in sync */

#define EST_JOIN_BEGIN         1
#define EST_JOIN_STOP_WAIT     2
#define EST_JOIN_ALL_STOPPED   3
#define EST_JOIN_START_WAIT    4
#define EST_JOIN_ALL_STARTED   5
#define EST_LEAVE_BEGIN        6
#define EST_LEAVE_STOP_WAIT    7
#define EST_LEAVE_ALL_STOPPED  8
#define EST_LEAVE_START_WAIT   9
#define EST_LEAVE_ALL_STARTED 10
#define EST_FAIL_BEGIN        11
#define EST_FAIL_STOP_WAIT    12
#define EST_FAIL_ALL_STOPPED  13
#define EST_FAIL_START_WAIT   14
#define EST_FAIL_ALL_STARTED  15

char *ev_state_str(state)
{
	switch (state) {
	case EST_JOIN_BEGIN:
		return "JOIN_BEGIN";
	case EST_JOIN_STOP_WAIT:
		return "JOIN_STOP_WAIT";
	case EST_JOIN_ALL_STOPPED:
		return "JOIN_ALL_STOPPED";
	case EST_JOIN_START_WAIT:
		return "JOIN_START_WAIT";
	case EST_JOIN_ALL_STARTED:
		return "JOIN_ALL_STARTED";
	case EST_LEAVE_BEGIN:
		return "LEAVE_BEGIN";
	case EST_LEAVE_STOP_WAIT:
		return "LEAVE_STOP_WAIT";
	case EST_LEAVE_ALL_STOPPED:
		return "LEAVE_ALL_STOPPED";
	case EST_LEAVE_START_WAIT:
		return "LEAVE_START_WAIT";
	case EST_LEAVE_ALL_STARTED:
		return "LEAVE_ALL_STARTED";
	case EST_FAIL_BEGIN:
		return "FAIL_BEGIN";
	case EST_FAIL_STOP_WAIT:
		return "FAIL_STOP_WAIT";
	case EST_FAIL_ALL_STOPPED:
		return "FAIL_ALL_STOPPED";
	case EST_FAIL_START_WAIT:
		return "FAIL_START_WAIT";
	case EST_FAIL_ALL_STARTED:
		return "FAIL_ALL_STARTED";
	default:
		return "unknown";
	}
}

char *state_str(group_data_t *data)
{
	static char buf[128];
	
	memset(buf, 0, sizeof(buf));

	if (!data->event_state && !data->event_nodeid)
		sprintf(buf, "none");
	else if (verbose)
		snprintf(buf, 127, "%s %d %llx %d",
			 ev_state_str(data->event_state),
			 data->event_nodeid,
			 (unsigned long long)data->event_id,
			 data->event_local_status);
	else
		snprintf(buf, 127, "%s", ev_state_str(data->event_state));

	return buf;
}

static int data_compare(const void *va, const void *vb)
{
	const group_data_t *a = va;
	const group_data_t *b = vb;
	return a->level - b->level;
}

static int member_compare(const void *va, const void *vb)
{
	const int *a = va;
	const int *b = vb;
	return *a - *b;
}

static int groupd_list(int argc, char **argv)
{
	group_data_t data[MAX_GROUPS];
	int i, j, rv, count = 0, level;
	char *name, *state_header;
	int type_width = 16;
	int level_width = 5;
	int name_width = 32;
	int id_width = 8;
	int state_width = 12;
	int len, max_name = 4;

	memset(&data, 0, sizeof(data));

	if (opt_ind && opt_ind < argc) {
		level = atoi(argv[opt_ind++]);
		name = argv[opt_ind];

		rv = group_get_group(level, name, data);
		count = 1;
	} else
		rv = group_get_groups(MAX_GROUPS, &count, data);

	if (rv < 0)
		return rv;

	if (!count)
		return 0;

	for (i = 0; i < count; i++) {
		len = strlen(data[i].name);
		if (len > max_name)
			max_name = len;
	}
	name_width = max_name + 1;

	if (verbose)
		state_header = "state node id local_done";
	else
		state_header = "state";
			
	qsort(&data, count, sizeof(group_data_t), data_compare);

	for (i = 0; i < count; i++) {
		if (!i)
			printf("%-*s %-*s %-*s %-*s %-*s\n",
			       type_width, "type",
			       level_width, "level",
			       name_width, "name",
			       id_width, "id",
			       state_width, state_header);

		printf("%-*s %-*d %-*s %0*x %-*s\n",
			type_width, data[i].client_name,
			level_width, data[i].level,
			name_width, data[i].name,
			id_width, data[i].id,
			state_width, state_str(&data[i]));

		qsort(&data[i].members, data[i].member_count,
		      sizeof(int), member_compare);

		printf("[");
		for (j = 0; j < data[i].member_count; j++) {
			if (j != 0)
				printf(" ");
			printf("%d", data[i].members[j]);
		}
		printf("]\n");
	}
	return 0;
}

static int fenced_node_compare(const void *va, const void *vb)
{
	const struct fenced_node *a = va;
	const struct fenced_node *b = vb;

	return a->nodeid - b->nodeid;
}

static void print_header(void)
{
	if (print_header_done)
		return;
	print_header_done = 1;

	printf("type         level name             id       state\n");
}

static void fenced_list(void)
{
	struct fenced_domain d;
	struct fenced_node nodes[MAX_NODES];
	struct fenced_node *np;
	int node_count;
	int rv, j;

	rv = fenced_domain_info(&d);
	if (rv < 0)
		return;

	print_header();

	printf("fence        0     %-*s %08x %d\n",
	       16, "default", 0, d.state);

	node_count = 0;
	memset(&nodes, 0, sizeof(nodes));

	rv = fenced_domain_nodes(FENCED_NODES_MEMBERS, MAX_NODES,
				 &node_count, nodes);
	if (rv < 0 || !node_count)
		goto do_nodeids;

	qsort(&nodes, node_count, sizeof(struct fenced_node),
	      fenced_node_compare);

 do_nodeids:
	printf("[");
	np = nodes;
	for (j = 0; j < node_count; j++) {
		if (j)
			printf(" ");
		printf("%d", np->nodeid);
		np++;
	}
	printf("]\n");
}

static int dlmc_node_compare(const void *va, const void *vb)
{
	const struct dlmc_node *a = va;
	const struct dlmc_node *b = vb;

	return a->nodeid - b->nodeid;
}

static void dlm_controld_list(void)
{
	struct dlmc_lockspace lss[MAX_LS];
	struct dlmc_node nodes[MAX_NODES];
	struct dlmc_node *np;
	struct dlmc_lockspace *ls;
	char *name = NULL;
	int node_count;
	int ls_count;
	int rv;
	int i, j;

	memset(lss, 0, sizeof(lss));

	if (name) {
		rv = dlmc_lockspace_info(name, lss);
		if (rv < 0)
			return;
		ls_count = 1;
	} else {
		rv = dlmc_lockspaces(MAX_LS, &ls_count, lss);
		if (rv < 0)
			return;
	}

	for (i = 0; i < ls_count; i++) {
		ls = &lss[i];

		if (!i)
			print_header();

		printf("dlm          1     %-*s %08x %x\n",
			16, ls->name, ls->global_id, ls->flags);

		node_count = 0;
		memset(&nodes, 0, sizeof(nodes));

		rv = dlmc_lockspace_nodes(ls->name, DLMC_NODES_MEMBERS,
					  MAX_NODES, &node_count, nodes);
		if (rv < 0 || !node_count)
			goto do_nodeids;

		qsort(nodes, node_count, sizeof(struct dlmc_node),
		      dlmc_node_compare);

 do_nodeids:
		printf("[");
		np = nodes;
		for (j = 0; j < node_count; j++) {
			if (j)
				printf(" ");
			printf("%d", np->nodeid);
			np++;
		}
		printf("]\n");
	}
}

static int gfsc_node_compare(const void *va, const void *vb)
{
	const struct gfsc_node *a = va;
	const struct gfsc_node *b = vb;

	return a->nodeid - b->nodeid;
}

static void gfs_controld_list(void)
{
	struct gfsc_mountgroup mgs[MAX_MG];
	struct gfsc_node nodes[MAX_NODES];
	struct gfsc_node *np;
	struct gfsc_mountgroup *mg;
	char *name = NULL;
	int node_count;
	int mg_count;
	int rv;
	int i, j;

	memset(mgs, 0, sizeof(mgs));

	if (name) {
		rv = gfsc_mountgroup_info(name, mgs);
		if (rv < 0)
			return;
		mg_count = 1;
	} else {
		rv = gfsc_mountgroups(MAX_MG, &mg_count, mgs);
		if (rv < 0)
			return;
	}

	for (i = 0; i < mg_count; i++) {
		mg = &mgs[i];

		if (!i)
			print_header();

		printf("gfs          2     %-*s %08x %x\n",
			16, mg->name, mg->global_id, mg->flags);

		node_count = 0;
		memset(&nodes, 0, sizeof(nodes));

		rv = gfsc_mountgroup_nodes(mg->name, GFSC_NODES_MEMBERS,
					   MAX_NODES, &node_count, nodes);
		if (rv < 0 || !node_count)
			goto do_nodeids;

		qsort(nodes, node_count, sizeof(struct gfsc_node),
		      gfsc_node_compare);

 do_nodeids:
		printf("[");
		np = nodes;
		for (j = 0; j < node_count; j++) {
			if (j)
				printf(" ");
			printf("%d", np->nodeid);
			np++;
		}
		printf("]\n");
	}
}

static int connect_daemon(char *path)
{
	struct sockaddr_un sun;
	socklen_t addrlen;
	int rv, fd;

	fd = socket(PF_UNIX, SOCK_STREAM, 0);
	if (fd < 0)
		goto out;

	memset(&sun, 0, sizeof(sun));
	sun.sun_family = AF_UNIX;
	strcpy(&sun.sun_path[1], path);
	addrlen = sizeof(sa_family_t) + strlen(sun.sun_path+1) + 1;

	rv = connect(fd, (struct sockaddr *) &sun, addrlen);
	if (rv < 0) {
		close(fd);
		fd = rv;
	}
 out:
	return fd;
}

static void groupd_dump_debug(int argc, char **argv, char *inbuf)
{
	char outbuf[GROUPD_MSGLEN];
	int rv, fd;

	fd = connect_daemon(GROUPD_SOCK_PATH);
	if (fd < 0)
		return;

	memset(outbuf, 0, sizeof(outbuf));
	sprintf(outbuf, "dump");

	rv = do_write(fd, outbuf, sizeof(outbuf));
	if (rv < 0) {
		printf("dump write error %d errno %d\n", rv, errno);
		return;
	}

	do_read(fd, inbuf, sizeof(inbuf));

	close(fd);
}

static int do_log(char *comment)
{
	char buf[GROUPD_MSGLEN];
	int fd, rv;

	fd = connect_daemon(GROUPD_SOCK_PATH);
	if (fd < 0)
		return fd;
	memset(buf, 0, sizeof(buf));
	snprintf(buf, sizeof(buf), "log %s", comment);
	rv = write(fd, &buf, GROUPD_MSGLEN);
	close(fd);
	return rv;
}

int main(int argc, char **argv)
{
	int rv, version;

	prog_name = argv[0];
	decode_arguments(argc, argv);

	switch (operation) {
	case OP_LIST:
		if (all_daemons) {
			if (verbose) {
				system("fence_tool ls -v");
				system("dlm_tool ls -v");
				system("gfs_control ls -v");
			} else {
				system("fence_tool ls");
				system("dlm_tool ls");
				system("gfs_control ls");
			}
		} else {
			rv = group_get_version(&version);

			if (version == -EAGAIN) {
				printf("groupd detecting version...\n");
			} else if (!rv && version == GROUP_LIBGROUP) {
				groupd_list(argc, argv);
			} else {
				fenced_list();
				dlm_controld_list();
				gfs_controld_list();
			}
		}
		break;

	case OP_DUMP:
		if (opt_ind && opt_ind < argc) {
			if (!strncmp(argv[opt_ind], "gfs", 3)) {
				char gbuf[GFSC_DUMP_SIZE];

				memset(gbuf, 0, sizeof(gbuf));

				printf("dump gfs\n");
				gfsc_dump_debug(gbuf);

				do_write(STDOUT_FILENO, gbuf, strlen(gbuf));
			}

			if (!strncmp(argv[opt_ind], "dlm", 3)) {
				char dbuf[DLMC_DUMP_SIZE];

				memset(dbuf, 0, sizeof(dbuf));

				printf("dump dlm\n");
				dlmc_dump_debug(dbuf);

				do_write(STDOUT_FILENO, dbuf, strlen(dbuf));
			}

			if (!strncmp(argv[opt_ind], "fence", 5)) {
				char fbuf[FENCED_DUMP_SIZE];

				memset(fbuf, 0, sizeof(fbuf));

				fenced_dump_debug(fbuf);

				do_write(STDOUT_FILENO, fbuf, strlen(fbuf));
			}

			if (!strncmp(argv[opt_ind], "plocks", 6)) {
				char pbuf[DLMC_DUMP_SIZE];

				if (opt_ind + 1 >= argc) {
					printf("plock dump requires name\n");
					return -1;
				}

				memset(pbuf, 0, sizeof(pbuf));

				dlmc_dump_plocks(argv[opt_ind + 1], pbuf);

				do_write(STDOUT_FILENO, pbuf, strlen(pbuf));
			}
		} else {
			char rbuf[GROUPD_DUMP_SIZE];

			memset(rbuf, 0, sizeof(rbuf));

			groupd_dump_debug(argc, argv, rbuf);

			do_write(STDOUT_FILENO, rbuf, strlen(rbuf));
		}

		break;

	case OP_LOG:
		if (opt_ind && opt_ind < argc) {
			return do_log(argv[opt_ind]);
		}
	}

	return 0;
}

