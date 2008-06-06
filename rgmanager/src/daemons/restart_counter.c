/* Time-based restart counters for rgmanager */

#include <stdio.h>
#include <list.h>
#include <errno.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <time.h>
#include <restart_counter.h>



#define RESTART_INFO_MAGIC 0x184820ab

typedef struct {
	list_head();
	time_t restart_time;
} restart_item_t;

typedef struct {
	int magic;
	time_t expire_timeout;
	int max_restarts;
	int restart_count;
	restart_item_t *restart_nodes;
} restart_info_t;


#define VALIDATE(arg, ret) \
do { \
	if (((restart_info_t *)arg)->magic != RESTART_INFO_MAGIC) {\
		errno = EINVAL; \
		return ret; \
	} \
} while(0)


/* Remove expired restarts */
static int
restart_timer_purge(restart_counter_t arg, time_t now)
{
	restart_info_t *restarts = (restart_info_t *)arg;
	restart_item_t *i;
	int x, done = 0;

	VALIDATE(arg, -1);

	/* No timeout */
	if (restarts->expire_timeout == 0)
		return 0;

	do {
		done = 1;
		list_for(&restarts->restart_nodes, i, x) {
			if ((now - i->restart_time) >=
			    restarts->expire_timeout) {
				restarts->restart_count--;
				list_remove(&restarts->restart_nodes, i);
				done = 0;
				break;
			}
		}
	} while(!done);

	return 0;
}


int
restart_count(restart_counter_t arg)
{
	restart_info_t *restarts = (restart_info_t *)arg;
	time_t now;

	VALIDATE(arg, -1);
	now = time(NULL);
	restart_timer_purge(arg, now);
	return restarts->restart_count;
}


/* Add a restart entry to the list.  Returns 1 if restart
   count is exceeded */
int
restart_add(restart_counter_t arg)
{
	restart_info_t *restarts = (restart_info_t *)arg;
	restart_item_t *i;
	time_t t;

	if (!arg)
		/* No max restarts / threshold = always
		   ok to restart! */
		return 0;

	VALIDATE(arg, -1);

	i = malloc(sizeof(*i));
	if (!i) {
		return -1;
	}

	t = time(NULL);
	i->restart_time = t;

	list_insert(&restarts->restart_nodes, i);
	restarts->restart_count++;

	/* Check and remove old entries */
	restart_timer_purge(restarts, t);

	if (restarts->restart_count > restarts->max_restarts)
		return 1;

	return 0;
}


int
restart_clear(restart_counter_t arg)
{
	restart_info_t *restarts = (restart_info_t *)arg;
	restart_item_t *i;

	VALIDATE(arg, -1);
	while ((i = restarts->restart_nodes)) {
		list_remove(&restarts->restart_nodes, i);
		free(i);
	}

	restarts->restart_count = 0;

	return 0;
}


restart_counter_t
restart_init(time_t expire_timeout, int max_restarts)
{
	restart_info_t *info;

	if (max_restarts < 0) {
		errno = EINVAL;
		return NULL;
	}

	info = malloc(sizeof(*info));
	if (info == NULL)
		return NULL;

	info->magic = RESTART_INFO_MAGIC;
	info->expire_timeout = expire_timeout;
	info->max_restarts = max_restarts;
	info->restart_count = 0;

	return (void *)info;
}


int
restart_cleanup(restart_counter_t arg)
{
	VALIDATE(arg, -1);
	restart_clear(arg);
	free(arg);
	return 0;
}
