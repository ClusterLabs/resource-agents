/*
  Copyright Red Hat, Inc. 2002-2003

  This program is free software; you can redistribute it and/or modify it
  under the terms of the GNU General Public License as published by the
  Free Software Foundation; either version 2, or (at your option) any
  later version.

  This program is distributed in the hope that it will be useful, but
  WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; see the file COPYING.  If not, write to the
  Free Software Foundation, Inc.,  675 Mass Ave, Cambridge,
  MA 02139, USA.
*/
/** @file
 * Multithreaded cluster locking test program.
 */
#include <pthread.h>
#include <magma.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <sys/time.h>


void *
thread_func(void *arg)
{
	void *lockp;
	int max = *(int *)arg;
	int ret, iter = 0;

	printf("Thread %u running %d iterations\n",(int)pthread_self(), 
	       max);

	while (iter < max) {
		ret = clu_lock("locky", CLK_EX, &lockp);

		if (ret != 0) {
			printf("Denied: %s\n", strerror(errno));
			sleep(1);
			continue;
		}

		++iter;
		clu_unlock("locky", lockp);
	}

	printf("Thread %u done\n", (int)pthread_self());
	return NULL;
}


int
main(int argc, char **argv)
{
	pthread_t th[10];
	int fd;
	int x, max = 1, iter = 200;
	void *ret;
	struct timeval start, end;

	if (argc == 1) {
		printf("Multithreaded Magma cluster locking test program\n");
		printf("\n");
		printf("usage: %s <num_threads> [lock_iterations]\n",
		       argv[0]);
		printf("   num_threads      1 to 10, default = 1\n");
		printf("   lock_iterations  1 to 2^31-1, default = 200\n");
		return 1;
	}

	if (argc >= 2) 
		max = atoi(argv[1]);
	if (max > 10)
		max = 10;
	if (argc >= 3)
		iter = atoi(argv[2]);
	if (iter < 1)
		iter = 1;

	fd = clu_connect("test::lock", 1);

	clu_null();

	printf("Requested %d threads, %d iterations\n", max, iter);

	gettimeofday(&start, NULL);

	for (x = 0; x < max; x++)
		pthread_create(&th[x], NULL, thread_func, &iter);
	
	for (x = 0; x < max; x++)
		pthread_join(th[x], &ret);

	gettimeofday(&end, NULL);

	clu_disconnect(fd);

	start.tv_sec = end.tv_sec - start.tv_sec;
	start.tv_usec = end.tv_usec - start.tv_usec;
	if (start.tv_usec < 0) {
		start.tv_sec--;
		start.tv_usec += 1000000;
	}

	printf("Time taken: %ld.%06ld seconds\n", (long)start.tv_sec,
	       (long)start.tv_usec);

	return 0;
}
