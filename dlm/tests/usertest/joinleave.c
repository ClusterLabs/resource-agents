#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stddef.h>
#include <errno.h>
#include <time.h>
#include <sys/time.h>
#include <syslog.h>
#include <sys/types.h>
#include <sys/errno.h>

#include "libdlm.h"

static dlm_lshandle_t *dh;

int main(int argc, char *argv[])
{
	struct timeval begin, end, diff;
	char *name;
	int sec = 0;

	if (argc < 2) {
		printf("%s <lockspace name> [sleep sec]\n", argv[0]);
		exit(-1);
	}

	name = argv[1];

	if (argc > 2)
		sec = atoi(argv[2]);

	printf("Joining lockspace \"%s\" ... ", name);
	fflush(stdout);

	gettimeofday(&begin, NULL);
	dh = dlm_create_lockspace(name, 0600);
	if (!dh) {
		printf("dlm_create_lockspace error %d %d\n", (int) dh, errno);
		return -ENOTCONN;
	}
	gettimeofday(&end, NULL);

	timersub(&end, &begin, &diff);
	printf("%lu s\n", diff.tv_sec);

	if (sec)
		sleep(sec);

	printf("Leaving lockspace \"%s\" ... ", name);
	fflush(stdout);

	gettimeofday(&begin, NULL);
	dlm_release_lockspace(name, dh, 1);
	gettimeofday(&end, NULL);

	timersub(&end, &begin, &diff);
	printf("%lu s\n", diff.tv_sec);

	return 0;
}

