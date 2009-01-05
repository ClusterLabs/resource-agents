#include <sys/types.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>
#include <corosync/corotypes.h>
#include <corosync/cmanquorum.h>

static cmanquorum_handle_t handle;

static char *node_state(int state)
{
	switch (state) {
	case NODESTATE_JOINING:
		return "Joining";
		break;
	case NODESTATE_MEMBER:
		return "Member";
		break;
	case NODESTATE_DEAD:
		return "Dead";
		break;
	case NODESTATE_LEAVING:
		return "Leaving";
		break;
	case NODESTATE_DISALLOWED:
		return "Disallowed";
		break;
	default:
		return "UNKNOWN";
		break;
	}
}

static void cmanquorum_notification_fn(
	cmanquorum_handle_t handle,
	uint32_t quorate,
	uint32_t node_list_entries,
	cmanquorum_node_t node_list[]
	)
{
	int i;

	printf("cmanquorum notification called \n");
	printf("  quorate         = %d\n", quorate);
	printf("  number of nodes = %d\n", node_list_entries);

	for (i = 0; i< node_list_entries; i++) {
		printf("      %d: %s\n", node_list[i].nodeid, node_state(node_list[i].state));
	}
	printf("\n");
}


int main(int argc, char *argv[])
{
	struct cmanquorum_info info;
	cmanquorum_callbacks_t callbacks;
	int err;

	if (argc > 1 && strcmp(argv[1], "-h")==0) {
		fprintf(stderr, "usage: %s [new-expected] [new-votes]\n", argv[0]);
		return 0;
	}

	callbacks.cmanquorum_notify_fn = cmanquorum_notification_fn;
	if ( (err=cmanquorum_initialize(&handle, &callbacks)) != CS_OK)
		fprintf(stderr, "cmanquorum_initialize FAILED: %d\n", err);

	if ( (err = cmanquorum_trackstart(handle, CS_TRACK_CHANGES)) != CS_OK)
		fprintf(stderr, "cmanquorum_trackstart FAILED: %d\n", err);

	if ( (err=cmanquorum_getinfo(handle, 0, &info)) != CS_OK)
		fprintf(stderr, "cmanquorum_getinfo FAILED: %d\n", err);
	else {
		printf("node votes       %d\n", info.node_votes);
		printf("expected votes   %d\n", info.node_expected_votes);
		printf("highest expected %d\n", info.highest_expected);
		printf("total votes      %d\n", info.total_votes);
		printf("quorum           %d\n", info.quorum);
		printf("flags            ");
		if (info.flags & CMANQUORUM_INFO_FLAG_DIRTY) printf("Dirty ");
		if (info.flags & CMANQUORUM_INFO_FLAG_DISALLOWED) printf("Disallowed ");
		if (info.flags & CMANQUORUM_INFO_FLAG_TWONODE) printf("2Node ");
		if (info.flags & CMANQUORUM_INFO_FLAG_QUORATE) printf("Quorate ");
		printf("\n");
	}

	if (argc >= 2 && atoi(argv[1])) {
		if ( (err=cmanquorum_setexpected(handle, atoi(argv[1]))) != CS_OK)
			fprintf(stderr, "set expected votes FAILED: %d\n", err);
	}
	if (argc >= 3 && atoi(argv[2])) {
		if ( (err=cmanquorum_setvotes(handle, 0, atoi(argv[2]))) != CS_OK)
			fprintf(stderr, "set votes FAILED: %d\n", err);
	}

	if (argc >= 2) {
		if ( (err=cmanquorum_getinfo(handle, 0, &info)) != CS_OK)
			fprintf(stderr, "cmanquorum_getinfo2 FAILED: %d\n", err);
		else {
			printf("-------------------\n");
			printf("node votes       %d\n", info.node_votes);
			printf("expected votes   %d\n", info.node_expected_votes);
			printf("highest expected %d\n", info.highest_expected);
			printf("total votes      %d\n", info.total_votes);
			printf("cmanquorum           %d\n", info.quorum);
			printf("flags            ");
			if (info.flags & CMANQUORUM_INFO_FLAG_DIRTY) printf("Dirty ");
			if (info.flags & CMANQUORUM_INFO_FLAG_DISALLOWED) printf("Disallowed ");
			if (info.flags & CMANQUORUM_INFO_FLAG_TWONODE) printf("2Node ");
			if (info.flags & CMANQUORUM_INFO_FLAG_QUORATE) printf("Quorate ");
			printf("\n");
		}
	}

	printf("Waiting for cmanquorum events, press ^C to finish\n");
	printf("-------------------\n");

	while (1)
		cmanquorum_dispatch(handle, CS_DISPATCH_ALL);

	return 0;
}
