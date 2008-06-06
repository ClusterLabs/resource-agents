#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include <time.h>
#include <sys/time.h>
#include <syslog.h>
#include <asm/types.h>
#include <sys/socket.h>
#include <sys/poll.h>
#include <sys/un.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/errno.h>

#include "libdlm.h"

#define die(fmt, args...) \
do \
{ \
  fprintf(stderr, "%s: ", prog_name); \
  fprintf(stderr, fmt, ##args); \
  exit(EXIT_FAILURE); \
} \
while (0)

static char *prog_name;
static dlm_lshandle_t *dh;
static int verbose;

static struct dlm_lksb lksb;
static char lvb[32];

int main(int argc, char *argv[])
{
	unsigned long long offset;
	unsigned long long num, last_num = 0;
	unsigned int id, clients, sleep = 0;
	unsigned long long skip = 0;
	char *name;
	int rv;

	prog_name = argv[0];
	verbose = 0;

	if (argc < 5)
		die("name offset id clients [sleep]\n");

	name = argv[1];
	offset = atoll(argv[2]);
	id = atoi(argv[3]);
	clients = atoi(argv[4]);

	if (argc > 5)
		sleep = atoi(argv[5]);

	printf("Joining \"alternate\" lockspace...\n");

	dh = dlm_create_lockspace("alternate", 0600);
	if (!dh) {
		printf("dlm_create_lockspace error %d %d\n", (int) dh, errno);
		return -ENOTCONN;
	}

	rv = dlm_ls_pthread_init(dh);
	if (rv < 0) {
		printf("dlm_ls_pthread_init error %d %d\n", rv, errno);
		dlm_release_lockspace("alternate", dh, 1);
		return rv;
	}

	memset(&lksb, 0, sizeof(lksb));
	memset(&lvb, 0, sizeof(lvb));
	lksb.sb_lvbptr = lvb;

	if (verbose)
		printf("request NL\n");

	rv = dlm_ls_lock_wait(dh, LKM_NLMODE, &lksb, LKF_VALBLK,
			      name, strlen(name), 0, NULL, NULL, NULL);

	while (1) {
		if (verbose)
			printf("convert NL->PR\n");

		rv = dlm_ls_lock_wait(dh, LKM_PRMODE, &lksb,
				      LKF_VALBLK | LKF_CONVERT,
			      	      name, strlen(name),
				      0, NULL, NULL, NULL);
		if (rv)
			printf("lock1 error: %d %d\n", rv, lksb.sb_status);

		memcpy(&num, &lvb, sizeof(num));

		if (verbose)
			printf("read lvb %llu\n", num);

		/* it's our turn */
		if (num % clients == id) {
			if (last_num && last_num + clients != num + 1)
				die("bad: num %llu last_num %llu\n",
				    num, last_num);

			if (verbose)
				printf("convert PR->EX\n");

			rv = dlm_ls_lock_wait(dh, LKM_EXMODE, &lksb,
				      	      LKF_VALBLK | LKF_CONVERT,
					      name, strlen(name),
					      0, NULL, NULL, NULL);
			if (rv)
				printf("lock2 error: %d %d\n", rv,
					lksb.sb_status);

			memcpy(&num, &lvb, sizeof(num));
			if (num % clients != id)
				die("bad2: num %llu\n", num);

			num++;

			memcpy(&lvb, &num, sizeof(num));
			printf("%llu %llu\n", num, skip);

			if (verbose)
				printf("convert EX->NL\n");

			rv = dlm_ls_lock_wait(dh, LKM_NLMODE, &lksb,
				      	      LKF_VALBLK | LKF_CONVERT,
					      name, strlen(name),
					      0, NULL, NULL, NULL);
			if (rv)
				printf("lock3 error: %d %d\n", rv,
					lksb.sb_status);

			last_num = num;
			skip = 0;
		} else {
			skip++;

			if (verbose)
				printf("convert PR->NL, skip %llu\n", skip);

			rv = dlm_ls_lock_wait(dh, LKM_NLMODE, &lksb,
				      	      LKF_VALBLK | LKF_CONVERT,
					      name, strlen(name),
					      0, NULL, NULL, NULL);
			if (rv)
				printf("lock4 error: %d %d\n", rv,
					lksb.sb_status);
		}

		if (sleep)
			usleep(sleep);
	}

	dlm_ls_unlock_wait(dh, lksb.sb_lkid, 0, &lksb);
	dlm_release_lockspace("alternate", dh, 1);

	exit(EXIT_SUCCESS);
}

