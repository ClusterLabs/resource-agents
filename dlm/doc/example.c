#include <stdlib.h>
#include <unistd.h>
#include <stdint.h>
#include <errno.h>
#include <string.h>
#include <stdio.h>
#include <signal.h>
#include <libdlm.h>

/*
 * Simple libdlm locking demo
 *
 * Daniel Phillips, phillips@redhat.com
 *
 */

#define error(string, args...) do { printf(string, ##args); exit(1); } while (0)

void my_ast(void *arg)
{
	printf("ast got arg %p\n", arg);
}

int main(void)
{
	int fd, child;
	struct dlm_lksb lksb;

	if ((fd = dlm_get_fd()) < 0)
		error("dlm error %i, %s\n", errno, strerror(errno));

	switch (child = fork()) {
	case -1:
		error("fork error %i, %s\n", errno, strerror(errno));
	case 0:
		while (1)
			dlm_dispatch(fd);
	}

	if (dlm_lock(LKM_PWMODE, &lksb, LKF_NOQUEUE, "foo", 3,
		     0, my_ast, (void *)&fd, NULL, NULL) < 0)
		error("dlm error %i, %s\n", errno, strerror(errno));
	sleep(1);

	if (dlm_unlock(lksb.sb_lkid, 0, &lksb, NULL) < 0)
		error("dlm error %i, %s\n", errno, strerror(errno));
	sleep(1);

	kill(child, SIGTERM);
	return 0;
}

