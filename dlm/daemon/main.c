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

#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <asm/types.h>
#include <sys/socket.h>
#include <sys/poll.h>
#include <sys/un.h>
#include <linux/netlink.h> 

/* FIXME: linux-2.6.11/include/linux/netlink.h (use header) */
#define NETLINK_KOBJECT_UEVENT  15

#define GROUPD_SOCK_PATH "groupd_socket"

#define MAXARGS 64
#define MAXLINE 256
#define MAXCON  4

#define log_error(fmt, args...) fprintf(stderr, fmt "\n", ##args)

int ls_stop(int argc, char **argv);
int ls_terminate(int argc, char **argv);
int ls_start(int argc, char **argv);
int ls_finish(int argc, char **argv);
int ls_set_id(int argc, char **argv);

int groupd_fd;
int uevent_fd;
int member_fd;


/* Detect new cluster members and set up their nodeid/addr values in
   dlm using the dlm-member ioctls.  This requires hooks into the
   membership manager.  Until this works, dlm_tool set_local/set_node
   must be run manually. */

void process_member(void)
{
}

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

/* read start/stop/finish messages from groupd and write to the corresponding
   /sys/kernel/dlm/<lsname>/ file; also global_id and members */

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

	printf("I: groupd read:  %s\n", buf);

	make_args(buf, &argc, argv, ' ');
	act = argv[0];

	/* Input from groupd (buf) should be the same string
	   as dlm_tool takes as input.  The ls_act() functions
	   don't want the first action arg so we have argc-1, argv+1.

	   FIXME: many more args than MAXARGS for start with nodeids */

	if (!strcmp(act, "stop")) {
		printf("O: ls_stop %s\n", argv[1]);
		rv = ls_stop(argc-1, argv+1);

	} else if (!strcmp(act, "start")) {
		printf("O: ls_start %s\n", argv[1]);
		rv = ls_start(argc-1, argv+1);

	} else if (!strcmp(act, "finish")) {
		printf("O: ls_finish %s\n", argv[1]);
		rv = ls_finish(argc-1, argv+1);

	} else if (!strcmp(act, "terminate")) {
		printf("O: ls_terminate %s\n", argv[1]);
		rv = ls_terminate(argc-1, argv+1);

	} else if (!strcmp(act, "set_id")) {
		printf("O: ls_set_id %s\n", argv[1]);
		rv = ls_set_id(argc-1, argv+1);

	} else
		log_error("unknown dlm control action: %s", act);

	if (rv)
		log_error("ls action error %d\n", rv);
}

/* recv "online" (join), "offline" (leave) and "change" (startdone)
   messages from dlm via uevents and pass them on to groupd */

int process_uevent(void)
{
	char buf[MAXLINE];
	char obuf[MAXLINE];
	char *argv[MAXARGS], *act, *name, *act_out;
	int rv, argc = 0;

	memset(buf, 0, sizeof(buf));

	rv = recv(uevent_fd, &buf, sizeof(buf), 0);
	if (rv < 0) {
		log_error("recv error %d errno %d", rv, errno);
		return -1;
	}

	if (!strstr(buf, "dlm"))
		return 0;

	printf("I: uevent recv:  %s\n", buf);

	make_args(buf, &argc, argv, '/');

	act = argv[0];
	name = argv[3];

	if (!strcmp(act, "online@"))
		act_out = "join";
	else if (!strcmp(act, "offline@"))
		act_out = "leave";
	else if (!strcmp(act, "change@"))
		act_out = "done";
	else {
		log_error("skip action %s", act);
		goto out;
	}

	memset(obuf, 0, sizeof(obuf));
	sprintf(obuf, "%s %s", act_out, name);

	printf("O: groupd write: %s\n", obuf);

	rv = write(groupd_fd, &obuf, strlen(obuf));
	if (rv < 0)
		log_error("write error %d errno %d %s", rv, errno, obuf);

 out:
	return 0;
}

void process_input(int fd)
{
	if (fd == groupd_fd)
		process_groupd();
	else if (fd == uevent_fd)
		process_uevent();
	else if (fd == member_fd)
		process_member();
}

void process_hup(int fd)
{
	if (fd == groupd_fd)
		close(groupd_fd);
	else if (fd == uevent_fd)
		close(uevent_fd);
}

int setup_groupd(void)
{
	char buf[] = "setup dlm 1";
	int rv;

	rv = write(groupd_fd, &buf, strlen(buf));
	if (rv < 0) {
		log_error("write error %d errno %d %s", rv, errno, buf);
		return -1;
	}
	return 0;
}

int loop(void)
{
	struct pollfd *pollfd;
	struct sockaddr_un sun;
	struct sockaddr_nl snl;
	socklen_t addrlen;
	int s, rv, i, maxi;


	pollfd = malloc(MAXCON * sizeof(struct pollfd));
	if (!pollfd)
		return -1;


	/* connect to the groupd server */

	s = socket(AF_LOCAL, SOCK_STREAM, 0);
	if (s < 0) {
		log_error("socket");
		goto out_free;
	}

	memset(&sun, 0, sizeof(sun));
	sun.sun_family = AF_LOCAL;
	strcpy(&sun.sun_path[1], GROUPD_SOCK_PATH);
	addrlen = sizeof(sa_family_t) + strlen(sun.sun_path+1) + 1;

	rv = connect(s, (struct sockaddr *) &sun, addrlen);
	if (rv < 0) {
		log_error("sun connect error %d errno %d", rv, errno);
		goto out_sun;
	}

	groupd_fd = s;
	pollfd[0].fd = s;
	pollfd[0].events = POLLIN;


	/* get uevents from the kernel */

	s = socket(AF_NETLINK, SOCK_DGRAM, NETLINK_KOBJECT_UEVENT);
	if (s < 0) {
		log_error("socket");
		goto out_sun;
	}

	memset(&snl, 0, sizeof(snl));
	snl.nl_family = AF_NETLINK;
	snl.nl_pid = getpid();
	snl.nl_groups = 1;

	rv = bind(s, (struct sockaddr *) &snl, sizeof(snl));
	if (rv < 0) {
		log_error("bind error %d errno %d", rv, errno);
		goto out_snl;
	}

	uevent_fd = s;
	pollfd[1].fd = s;
	pollfd[1].events = POLLIN;

	maxi = 1;

	rv = setup_groupd();
	if (rv < 0) {
		log_error("setup_groupd");
		goto out_snl;
	}

	for (;;) {
		rv = poll(pollfd, maxi + 1, -1);
		if (rv < 0)
			log_error("poll");

		for (i = 0; i <= maxi; i++) {
			if (pollfd[i].revents & POLLHUP) {
				process_hup(pollfd[i].fd);
			} else if (pollfd[i].revents & POLLIN) {
				process_input(pollfd[i].fd);
			}
		}
	}

out_snl:
out_sun:
out_free:
	free(pollfd);
	return 0;
}

int main(int argc, char **argv)
{
	return loop();
}

