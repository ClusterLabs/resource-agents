#include <lock.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <pthread.h>
#include <signal.h>


void *
lock_thread(void *arg)
{
	struct dlm_lksb lksb;

	while(1) {
		printf("Taking lock..\n");
		clu_lock(LKM_EXMODE, &lksb, 0, arg);
		printf("Thread acquired lock on %s\n", (char *)arg);
		clu_unlock(&lksb);
	}
}




int
main(int argc, char **argv)
{
	struct dlm_lksb lksb;
	int ret;
	pthread_t th;

	if (clu_lock_init("Testing") != 0) {
		perror("clu_lock_init");
		return 1;
	}

	if (argc < 2) {
		printf("Lock what?\n");
		return 1;
	}

	if (argc == 3) {
		pthread_create(&th, NULL, lock_thread, strdup(argv[1]));
	}

	memset(&lksb,0,sizeof(lksb));
	ret = clu_lock(LKM_EXMODE, &lksb, 0, argv[1]);
	if (ret < 0) {
		perror("clu_lock");
		return 1;
	}

	printf("Acquired lock on %s; press enter to release\n", argv[1]);
	getchar();

	clu_unlock(&lksb);

	if (argc == 3) {
		printf("Press enter to kill lock thread...\n");
		getchar();
		pthread_kill(th, SIGTERM);
	}

	clu_lock_finished("Testing");

	return 0;
}
