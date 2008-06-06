#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <inttypes.h>
#include <string.h>
#include <errno.h>
#include "gnbd_utils.h"
#include "member_cman.h"

cman_handle_t	ch;
int		cman_cb;
int		cman_reason;

static void member_callback(cman_handle_t h, void *private, int reason, int arg)
{
	cman_cb = 1;
	cman_reason = reason;

	if (reason == CMAN_REASON_TRY_SHUTDOWN) {
		if (can_shutdown(private))
			cman_replyto_shutdown(ch, 1);
		else {
			log_msg("no to cman shutdown");
			cman_replyto_shutdown(ch, 0);
		}
	}
}

int default_process_member(void)
{
	int rv;

	while (1) {
		rv = cman_dispatch(ch, CMAN_DISPATCH_ONE);
		if (rv < 0)
			break;

		if (cman_cb)
			cman_cb = 0;
		else
			break;
	}

	if (rv == -1 && errno == EHOSTDOWN) {
		log_err("cluster is down, exiting");
		exit(1);
	}
        return 0;
}

int setup_member(void *private)
{
	int fd;

	ch = cman_init(private);
	if (!ch) {
		log_err("cman_init error %d %d", (int) ch, errno);
		return -ENOTCONN;
	}

	if (cman_start_notification(ch, member_callback) < 0){
		log_err("cman_start_notification error : %s\n", 
			strerror(errno));
		cman_finish(ch);
		return -1;
	}

	fd = cman_get_fd(ch);

	return fd;
}

void exit_member(void)
{
	cman_finish(ch);
}
