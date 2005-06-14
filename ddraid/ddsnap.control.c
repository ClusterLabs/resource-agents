#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
//#include <fcntl.h>
#include <errno.h>
#include <inttypes.h>
#include <netinet/in.h>
#include "ddsnap.h"
#include "../dm-ddsnap.h"
#include "trace.h"
#include "sock.h"

#ifdef DELETE
#  define THIS_CODE DELETE_SNAPSHOT
#  define THIS_REPLY REPLY_DELETE_SNAPSHOT
#else
#  define THIS_CODE CREATE_SNAPSHOT
#  define THIS_REPLY REPLY_CREATE_SNAPSHOT
#endif

int main(int argc, char *argv[])
{
	int sock, err;
	struct head head;
	unsigned maxbuf = 500;
	char buf[maxbuf];

	if (argc < 3)
		error("usage: %s host:port snapshot", argv[0]);

	int snap = atoi(argv[2]);
	char *host = argv[1];
	int len = strlen(host), port = parse_port(host, &len);

	if (port < 0)
		error("expected host:port, not %s", host);
	host[len] = 0;

	if (!(sock = open_socket(host, port)))
		error("Can't connect to %s:%i", host, port);

	if (outbead(sock, THIS_CODE, struct create_snapshot, snap))
		goto eek;

	if ((err = readpipe(sock, &head, sizeof(head))))
		goto eek;

	assert(head.length < maxbuf); // !!! don't die
	if ((err = readpipe(sock, buf, head.length)))
		goto eek;
	trace_on(printf("reply = %x\n", head.code);)
	err  = head.code != THIS_REPLY;

	if (head.code == REPLY_ERROR)
		error("%.*s", head.length - 4, buf + 4);
	return 0;

eek:
	error("%s (%i)", strerror(errno), errno);
	return 0;
}
