#include <sys/socket.h>
#include <netdb.h>

/*
 * Find and return the port number of a host:port pair, shortening
 * the original string to include only the hostname.
 */
static inline int parse_port(char *s, unsigned *len)
{
	char *p = memchr(s, ':', *len);
	if (!p || p == s || p - s == *len)
		return -1;
	*len = p - s;
	return atoi(p + 1);
}

/*
 * Dumbed down interface for opening an IPv4 connection.
 */
static inline int open_socket(char *name, unsigned port)
{
	struct sockaddr_in addr = { .sin_family = AF_INET, .sin_port = htons(port) };
	struct hostent *host;
	int sock;

	if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0)
		error("Can't get socket");
	if (!(host = gethostbyname(name)))
		error("Unknown host '%s'", name);
	memcpy(&addr.sin_addr.s_addr, host->h_addr, host->h_length);
	if (connect(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0)
		error("Cannot connect to %s:%i", name, port);
	return sock;
}

/*
 * Pass a fd over a local socket connection.  You have to send some stream
 * data as well, just to make an ugly interface even more irritating.
 */
int send_fd(int sock, int fd, char *bogus, unsigned len)
{
	char payload[CMSG_SPACE(sizeof(int))];
	struct msghdr msg = {
		.msg_control = payload,
		.msg_controllen = sizeof(payload),
		.msg_iov = &(struct iovec){ .iov_base = bogus, .iov_len = len },
		.msg_iovlen = 1,
	};
	struct cmsghdr *cmsg = CMSG_FIRSTHDR(&msg);

	*cmsg = (struct cmsghdr){ CMSG_LEN(sizeof(int)), SOL_SOCKET, SCM_RIGHTS };
	*((int *)CMSG_DATA(cmsg)) = fd; // this is really an array, .cmsg_len gives count (??)

	return sendmsg(sock, &msg, 0) != len? -EIO: len;
}
