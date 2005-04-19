/******************************************************************************
*******************************************************************************
**
**  Copyright (C) 2005 Red Hat, Inc.  All rights reserved.
**
**  This copyrighted material is made available to anyone wishing to use,
**  modify, copy, or redistribute it subject to the terms and conditions
**  of the GNU General Public License v.2.
**
*******************************************************************************
******************************************************************************/

#include "lock_dlm.h"

extern struct list_head mounts;

int groupd_fd;
int uevent_fd;


void make_args(char *buf, int *argc, char **argv, char sep)
{
	char *p = buf;
	int i;

	argv[0] = p;

	for (i = 1; i < MAXARGS; i++) {
		p = strchr(buf, sep);
		if (!p)
			break;
		*p = '\0';
		argv[i] = p + 1;
		buf = p + 1;
	}
	*argc = i;
}

/* read start/stop/finish messages from groupd */

void process_groupd(void)
{
	char buf[MAXLINE];
	char *argv[MAXARGS], *act;
	int rv, argc = 0;

	memset(buf, 0, sizeof(buf));

	rv = read(groupd_fd, &buf, sizeof(buf));
	if (rv < 0) {
		log_error("read error %d errno %d", rv, errno);
		return;
	}

	log_debug("groupd read:  %s", buf);

	make_args(buf, &argc, argv, ' ');
	act = argv[0];

	/* Input from groupd (buf) should be the same string
	   as dlm_tool takes as input.  The do_act() functions
	   don't want the first action arg so we have argc-1, argv+1.

	   FIXME: many more args than MAXARGS for start with nodeids */

	if (!strcmp(act, "stop"))
		rv = do_stop(argc-1, argv+1);

	else if (!strcmp(act, "start"))
		rv = do_start(argc-1, argv+1);

	else if (!strcmp(act, "finish"))
		rv = do_finish(argc-1, argv+1);

	else if (!strcmp(act, "terminate"))
		rv = do_terminate(argc-1, argv+1);

	else
		log_error("unknown lock_dlm control action: %s", act);

	if (rv)
		log_error("action %s error %d", act, rv);
}

/* recv "online" (mount), "offline" (unmount) and "change" (recovery_done)
   messages from lock_dlm via uevents */

int process_uevent(void)
{
	char buf[MAXLINE];
	char *argv[MAXARGS], *act;
	int rv, argc = 0;

	memset(buf, 0, sizeof(buf));

	rv = recv(uevent_fd, &buf, sizeof(buf), 0);
	if (rv < 0) {
		log_error("recv error %d errno %d", rv, errno);
		return -1;
	}

	if (!strstr(buf, "lock_dlm"))
		return 0;

	log_debug("uevent recv:  %s", buf);

	make_args(buf, &argc, argv, '/');

	act = argv[0];

	if (!strcmp(act, "online@"))
		do_mount(argv[3]);

	else if (!strcmp(act, "offline@"))
		do_unmount(argv[3]);

	else if (!strcmp(act, "change@"))
		do_recovery_done(argv[3]);

	return 0;
}

int setup_groupd(void)
{
	struct sockaddr_un sun;
	socklen_t addrlen;
	char buf[] = "setup lock_dlm 2";
	int s, rv;

	s = socket(AF_LOCAL, SOCK_STREAM, 0);
	if (s < 0) {
		log_error("local socket");
		return s;
	}

	memset(&sun, 0, sizeof(sun));
	sun.sun_family = AF_LOCAL;
	strcpy(&sun.sun_path[1], GROUPD_SOCK_PATH);
	addrlen = sizeof(sa_family_t) + strlen(sun.sun_path+1) + 1;

	rv = connect(s, (struct sockaddr *) &sun, addrlen);
	if (rv < 0) {
		log_error("groupd connect error %d errno %d", rv, errno);
		close(s);
		return rv;
	}

	rv = write(s, &buf, strlen(buf));
	if (rv < 0) {
		log_error("groupd write error %d errno %d %s", rv, errno, buf);
		close(s);
		return rv;
	}

	return s;
}

int setup_uevent(void)
{
	struct sockaddr_nl snl;
	int s, rv;

	s = socket(AF_NETLINK, SOCK_DGRAM, NETLINK_KOBJECT_UEVENT);
	if (s < 0) {
		log_error("netlink socket");
		return s;
	}

	memset(&snl, 0, sizeof(snl));
	snl.nl_family = AF_NETLINK;
	snl.nl_pid = getpid();
	snl.nl_groups = 1;

	rv = bind(s, (struct sockaddr *) &snl, sizeof(snl));
	if (rv < 0) {
		log_error("uevent bind error %d errno %d", rv, errno);
		close(s);
		return rv;
	}

	return s;
}

int loop(void)
{
	struct pollfd *pollfd;
	int rv, i, maxi;


	pollfd = malloc(MAXCON * sizeof(struct pollfd));
	if (!pollfd)
		return -1;

	rv = setup_member();
	if (rv < 0) {
		log_error("setup_member error %d", rv);
		goto out;
	}

	groupd_fd = setup_groupd();
	pollfd[0].fd = groupd_fd;
	pollfd[0].events = POLLIN;

	uevent_fd = setup_uevent();
	pollfd[1].fd = uevent_fd;
	pollfd[1].events = POLLIN;

	maxi = 1;

	log_debug("groupd_fd %d uevent_fd %d", groupd_fd, uevent_fd);

	for (;;) {
		rv = poll(pollfd, maxi + 1, -1);
		if (rv < 0)
			log_error("poll");

		for (i = 0; i <= maxi; i++) {
			if (pollfd[i].revents & POLLIN) {
				if (pollfd[i].fd == groupd_fd)
					process_groupd();
				else if (pollfd[i].fd == uevent_fd)
					process_uevent();
			}

			if (pollfd[i].revents & POLLHUP) {
				log_error("closing fd %d", pollfd[i].fd);
				close(pollfd[i].fd);
			}
		}
	}

 out:
	free(pollfd);
	return 0;
}

int main(int argc, char **argv)
{
	INIT_LIST_HEAD(&mounts);
	return loop();
}

