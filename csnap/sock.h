#include <sys/socket.h>
#include <sys/ioctl.h>
#include <netdb.h>

static inline int open_socket(char *name, unsigned port)
{
	struct sockaddr_in addr = { .sin_family = AF_INET, .sin_port = htons(port) };
	struct hostent *host;
	int sock;

	if ((sock = socket(AF_INET,  SOCK_STREAM, 0)) < 0)
		error("Can't get socket");
	if (!(host = gethostbyname(name)))
		error("Unknown host '%s'", name);
	memcpy(&addr.sin_addr.s_addr, host->h_addr, host->h_length);
	if (connect(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0)
		error("Cannot connect to %s:%i", name, port);
	return sock;
}

static inline char *cscan(char *s, unsigned l, char c)
{
	while (l-- && *s++ != c)
		continue;
	return s;
}

static inline int strscan_port(char *s, unsigned *len)
{
	char *p = cscan(s, *len, ':');

	if (p == s || p - s == *len) {
		*len = 0;
		return -1;
	}
	*len = p - s - 1;
	return atoi(p);
}
