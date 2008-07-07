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

#include "libgfscontrol.h"

#define OPTION_STRING			"vhV"

#define OP_LIST				1
#define OP_DUMP				2
#define OP_PLOCKS			3
#define OP_JOIN				4
#define OP_LEAVE			5
#define OP_JOINLEAVE			6

static char *prog_name;
static char *fsname;
static int operation;
static int opt_ind;
static int verbose;

static void print_usage(void)
{
	printf("Usage:\n");
	printf("\n");
	printf("%s [options] [ls|dump|plocks]\n", prog_name);
	printf("\n");
	printf("Options:\n");
	printf("  -v               Verbose output\n");
	printf("  -h               Print this help, then exit\n");
	printf("  -V               Print program version information, then exit\n");
	printf("\n");
}

static void decode_arguments(int argc, char **argv)
{
	int cont = 1;
	int optchar;
	int need_fsname;

	while (cont) {
		optchar = getopt(argc, argv, OPTION_STRING);

		switch (optchar) {
		case 'h':
			print_usage();
			exit(EXIT_SUCCESS);
			break;

		case 'V':
			printf("%s %s (built %s %s)\n",
				prog_name, RELEASE_VERSION, __DATE__, __TIME__);
			/* printf("%s\n", REDHAT_COPYRIGHT); */
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

	need_fsname = 1;

	while (optind < argc) {

		if (!strncmp(argv[optind], "leave", 5) &&
			   (strlen(argv[optind]) == 5)) {
			operation = OP_LEAVE;
			opt_ind = optind + 1;
			break;
		} else if (!strncmp(argv[optind], "ls", 2) &&
			   (strlen(argv[optind]) == 2)) {
			operation = OP_LIST;
			opt_ind = optind + 1;
			need_fsname = 0;
			break;
		} else if (!strncmp(argv[optind], "dump", 4) &&
			   (strlen(argv[optind]) == 4)) {
			operation = OP_DUMP;
			opt_ind = optind + 1;
			need_fsname = 0;
			break;
		} else if (!strncmp(argv[optind], "plocks", 6) &&
			   (strlen(argv[optind]) == 6)) {
			operation = OP_PLOCKS;
			opt_ind = optind + 1;
			break;
		}

		optind++;
	}

	if (!operation || !opt_ind) {
		print_usage();
		exit(EXIT_FAILURE);
	}

	if (optind < argc - 1)
		fsname = argv[opt_ind];
	else if (need_fsname) {
		fprintf(stderr, "fs name required\n");
		exit(EXIT_FAILURE);
	}
}

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

void do_leave(char *table)
{
	struct gfsc_mount_args ma;
	int rv;

	memset(&ma, 0, sizeof(ma));

	strncpy(ma.table, table, sizeof(ma.table));

	rv = gfsc_fs_leave(&ma, 0);
	if (rv < 0)
		fprintf(stderr, "gfs_controld leave error %d\n", rv);
}

char *gfsc_mf_str(uint32_t flags)
{
	static char str[128];

	memset(str, 0, sizeof(str));

	if (flags & GFSC_MF_JOINING)
		strcat(str, "joining ");
	if (flags & GFSC_MF_LEAVING)
		strcat(str, "leaving ");
	if (flags & GFSC_MF_KERNEL_STOPPED)
		strcat(str, "kernel_stopped ");
	if (flags & GFSC_MF_KERNEL_MOUNT_DONE)
		strcat(str, "kernel_mount_done ");
	if (flags & GFSC_MF_KERNEL_MOUNT_ERROR)
		strcat(str, "kernel_mount_error ");
	if (flags & GFSC_MF_FIRST_RECOVERY_NEEDED)
		strcat(str, "first_recovery_needed ");
	if (flags & GFSC_MF_FIRST_RECOVERY_MSG)
		strcat(str, "first_recovery_msg ");
	if (flags & GFSC_MF_LOCAL_RECOVERY_BUSY)
		strcat(str, "local_recovery_busy ");

	return str;
}

char *gfsc_nf_str(uint32_t flags)
{
	static char str[128];

	memset(str, 0, sizeof(str));

	if (flags & GFSC_NF_MEMBER)
		strcat(str, "member ");
	if (flags & GFSC_NF_START)
		strcat(str, "start ");
	if (flags & GFSC_NF_DISALLOWED)
		strcat(str, "disallowed ");
	if (flags & GFSC_NF_KERNEL_MOUNT_DONE)
		strcat(str, "kernel_mount_done ");
	if (flags & GFSC_NF_KERNEL_MOUNT_ERROR)
		strcat(str, "kernel_mount_error ");
	if (flags & GFSC_NF_READONLY)
		strcat(str, "readonly ");
	if (flags & GFSC_NF_SPECTATOR)
		strcat(str, "spectator ");
	if (flags & GFSC_NF_CHECK_DLM)
		strcat(str, "check_dlm ");

	return str;
}

char *condition_str(int cond)
{
	switch (cond) {
	case 0:
		return "";
	case 1:
		return "kernel_mount_done";
	case 2:
		return "notify_nodeid";
	case 3:
		return "poll_dlm";
	case 4:
		return "pending";
	default:
		return "unknown";
	}
}

static void show_mg(struct gfsc_mountgroup *mg)
{
	printf("gfs mountgroup \"%s\"\n", mg->name);
	printf("id 0x%x flags 0x%x %s\n", mg->global_id, mg->flags,
		gfsc_mf_str(mg->flags));
	printf("journals needing recovery %d\n", mg->journals_need_recovery);

	printf("seq %u-%u counts member %d joined %d remove %d failed %d\n",
	        mg->cg_prev.combined_seq, mg->cg_prev.seq,
		mg->cg_prev.member_count, mg->cg_prev.joined_count,
		mg->cg_prev.remove_count, mg->cg_prev.failed_count);

	if (!mg->cg_next.seq)
		return;

	printf("new seq %u-%u counts member %d joined %d remove %d failed %d\n",
	        mg->cg_next.combined_seq, mg->cg_next.seq,
		mg->cg_next.member_count, mg->cg_next.joined_count,
		mg->cg_next.remove_count, mg->cg_next.failed_count);

	printf("new wait_messages %d wait_condition %d %s\n",
		mg->cg_next.wait_messages, mg->cg_next.wait_condition,
		condition_str(mg->cg_next.wait_condition));
}

static void show_all_nodes(int count, struct gfsc_node *nodes)
{
	struct gfsc_node *n = nodes;
	int i;

	for (i = 0; i < count; i++) {
		printf("nodeid %d jid %d add_seq %u rem_seq %u failed %d flags 0x%x %s\n",
			n->nodeid, n->jid, n->added_seq, n->removed_seq,
			n->failed_reason, n->flags, gfsc_nf_str(n->flags));
		n++;
	}
}

static void show_nodeids(int count, struct gfsc_node *nodes)
{
	struct gfsc_node *n = nodes;
	int i;

	for (i = 0; i < count; i++) {
		printf("%d ", n->nodeid);
		n++;
	}
	printf("\n");
}

static int node_compare(const void *va, const void *vb)
{
	const struct gfsc_node *a = va;
	const struct gfsc_node *b = vb;

	return a->nodeid - b->nodeid;
}

#define MAX_MG 128
#define MAX_NODES 128

struct gfsc_mountgroup mgs[MAX_MG];
struct gfsc_node nodes[MAX_NODES];

static void do_list(char *name)
{
	struct gfsc_mountgroup *mg;
	int node_count;
	int mg_count;
	int rv;
	int i;

	memset(mgs, 0, sizeof(mgs));

	if (name) {
		rv = gfsc_mountgroup_info(name, mgs);
		if (rv < 0)
			goto out;
		mg_count = 1;
	} else {
		rv = gfsc_mountgroups(MAX_MG, &mg_count, mgs);
		if (rv < 0)
			goto out;
	}

	for (i = 0; i < mg_count; i++) {
		mg = &mgs[i];

		show_mg(mg);

		node_count = 0;
		memset(&nodes, 0, sizeof(nodes));

		rv = gfsc_mountgroup_nodes(mg->name, GFSC_NODES_MEMBERS,
					   MAX_NODES, &node_count, nodes);
		if (rv < 0)
			goto out;

		qsort(nodes, node_count, sizeof(struct gfsc_node),node_compare);

		printf("members ");
		show_nodeids(node_count, nodes);

		if (!mg->cg_next.seq)
			goto show_all;

		node_count = 0;
		memset(&nodes, 0, sizeof(nodes));

		rv = gfsc_mountgroup_nodes(mg->name, GFSC_NODES_NEXT,
					   MAX_NODES, &node_count, nodes);
		if (rv < 0)
			goto out;

		qsort(nodes, node_count, sizeof(struct gfsc_node),node_compare);

		printf("new members ");
		show_nodeids(node_count, nodes);

 show_all:
		if (!verbose)
			continue;

		node_count = 0;
		memset(&nodes, 0, sizeof(nodes));

		rv = gfsc_mountgroup_nodes(mg->name, GFSC_NODES_ALL,
					   MAX_NODES, &node_count, nodes);
		if (rv < 0)
			goto out;

		qsort(nodes, node_count, sizeof(struct gfsc_node),node_compare);

		printf("all nodes\n");
		show_all_nodes(node_count, nodes);
	}
	return;
 out:
	fprintf(stderr, "gfs_controld query error %d\n", rv);

}

static void do_plocks(char *name)
{
	char buf[GFSC_DUMP_SIZE];

	memset(buf, 0, sizeof(buf));

	gfsc_dump_plocks(name, buf);

	do_write(STDOUT_FILENO, buf, strlen(buf));
}

static void do_dump(void)
{
	char buf[GFSC_DUMP_SIZE];

	memset(buf, 0, sizeof(buf));

	gfsc_dump_debug(buf);

	do_write(STDOUT_FILENO, buf, strlen(buf));
}

int main(int argc, char **argv)
{
	prog_name = argv[0];
	decode_arguments(argc, argv);

	switch (operation) {

	case OP_LEAVE:
		do_leave(fsname);
		break;

	case OP_LIST:
		do_list(fsname);
		break;

	case OP_DUMP:
		do_dump();
		break;

	case OP_PLOCKS:
		do_plocks(fsname);
		break;
	}
	return 0;
}

