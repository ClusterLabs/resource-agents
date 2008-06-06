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

#define GROUPD_SOCK_PATH "groupd_socket"
#define COMMAND_SOCK_PATH "command_socket"

#define MAXLINE 256
#define MAXCON  4

#define log_error(fmt, args...) fprintf(stderr, fmt "\n", ##args)

int groupd_fd;
int command_fd;
struct sockaddr_un sun;
struct sockaddr_un addr;
socklen_t addrlen;


void process_groupd(void)
{
	char buf[MAXLINE];
	int rv;

	memset(buf, 0, sizeof(buf));

	rv = read(groupd_fd, &buf, sizeof(buf));
	if (rv < 0) {
		log_error("read error %d errno %d", rv, errno);
		return;
	}

	printf("I: groupd read:   %s\n", buf);
}

void process_command(void)
{
	char buf[MAXLINE];
	int rv;

	memset(buf, 0, sizeof(buf));

	rv = recvfrom(command_fd, buf, MAXLINE, 0, (struct sockaddr *) &addr,
		      &addrlen);

	printf("I: command recv:  %s\n", buf);

	rv = write(groupd_fd, buf, strlen(buf));
	if (rv < 0)
		log_error("groupd write error");

	printf("O: command write: %s\n", buf);
}

void process_input(int fd)
{
	if (fd == groupd_fd)
		process_groupd();
	else if (fd == command_fd)
		process_command();
}

void process_hup(int fd)
{
	if (fd == groupd_fd)
		close(groupd_fd);
	else if (fd == command_fd)
		close(command_fd);
}

int setup_groupd(void)
{
	char buf[] = "setup test 1";
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
	int s, rv, i, maxi;


	pollfd = malloc(MAXCON * sizeof(struct pollfd));
	if (!pollfd)
		return -1;


	/* connect to the groupd server */

	s = socket(AF_LOCAL, SOCK_STREAM, 0);
	if (s < 0) {
		log_error("socket");
		exit(1);
	}

	memset(&sun, 0, sizeof(sun));
	sun.sun_family = AF_LOCAL;
	strcpy(&sun.sun_path[1], GROUPD_SOCK_PATH);
	addrlen = sizeof(sa_family_t) + strlen(sun.sun_path+1) + 1;

	rv = connect(s, (struct sockaddr *) &sun, addrlen);
	if (rv < 0) {
		log_error("groupd connect error %d errno %d", rv, errno);
		exit(1);
	}

	groupd_fd = s;
	pollfd[0].fd = s;
	pollfd[0].events = POLLIN;


	/* get commands via another local socket */

	s = socket(AF_LOCAL, SOCK_DGRAM, 0);
	if (s < 0) {
		log_error("socket");
		exit(1);
	}

	memset(&addr, 0, sizeof(addr));
	addr.sun_family = AF_LOCAL;
	strcpy(&addr.sun_path[1], COMMAND_SOCK_PATH);
	addrlen = sizeof(sa_family_t) + strlen(addr.sun_path+1) + 1;

	rv = bind(s, (struct sockaddr *) &addr, addrlen);
	if (rv < 0) {
		log_error("bind error");
		exit(1);
	}

	command_fd = s;
	pollfd[1].fd = s;
	pollfd[1].events = POLLIN;

	maxi = 1;

	rv = setup_groupd();
	if (rv < 0) {
		log_error("setup_groupd");
		exit(1);
	}

	for (;;) {
		rv = poll(pollfd, maxi + 1, -1);
		if (rv < 0)
			log_error("poll");

		for (i = 0; i <= maxi; i++) {
			if (pollfd[i].revents & (POLLHUP | POLLERR | POLLNVAL)) {
				process_hup(pollfd[i].fd);
			} else if (pollfd[i].revents & POLLIN) {
				process_input(pollfd[i].fd);
			}
		}
	}

	free(pollfd);
	return 0;
}

int main(int argc, char **argv)
{
	return loop();
}

