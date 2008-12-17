#include <sys/types.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>
#include <corosync/corotypes.h>
#include <corosync/cmanquorum.h>

static cmanquorum_handle_t handle;


static void print_info(int ok_to_fail)
{
	struct cmanquorum_qdisk_info qinfo;
	int err;

	if ( (err=cmanquorum_qdisk_getinfo(handle, &qinfo)) != CS_OK)
		fprintf(stderr, "cmanquorum_qdisk_getinfo error %d: %s\n", err, ok_to_fail?"OK":"FAILED");
	else {
		printf("qdisk votes  %d\n", qinfo.votes);
		printf("state        %d\n", qinfo.state);
		printf("name         %s\n", qinfo.name);
		printf("\n");
	}
}

int main(int argc, char *argv[])
{
	int pollcount=0, polltime=1;
	int err;

	if ( (err=cmanquorum_initialize(&handle, NULL)) != CS_OK) {
		fprintf(stderr, "cmanquorum_initialize FAILED: %d\n", err);
		return -1;
	}

	print_info(1);

	if (argc >= 2 && atoi(argv[1])) {
		pollcount = atoi(argv[1]);
	}
	if (argc >= 3 && atoi(argv[2])) {
		polltime = atoi(argv[2]);
	}

	if (argc >= 2) {
		if ( (err=cmanquorum_qdisk_register(handle, "QDISK", 4)) != CS_OK)
			fprintf(stderr, "qdisk_register FAILED: %d\n", err);

		while (pollcount--) {
			print_info(0);
			if ((err=cmanquorum_qdisk_poll(handle, 1)) != CS_OK)
				fprintf(stderr, "qdisk poll FAILED: %d\n", err);
			print_info(0);
			sleep(polltime);
		}
		if ((err= cmanquorum_qdisk_unregister(handle)) != CS_OK)
			fprintf(stderr, "qdisk unregister FAILED: %d\n", err);
	}
	print_info(1);

	return 0;
}
