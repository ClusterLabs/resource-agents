#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
//#include <fcntl.h>
#include <errno.h>
#include <inttypes.h>
#include <netinet/in.h>
#include "../dm-csnap.h"
#include "csnap.h"
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
	if (argc < 3)
		error("usage: %s host:port snapshot", argv[0]);

	int snap = atoi(argv[2]);
	char *host = argv[1];
	int len = strlen(host), port = strscan_port(host, &len);

	if (!len)
		error("expected host:port, not %s", host);
	host[len] = 0;

	if (!(sock = open_socket(host, port)))
		error("Can't connect to %s:%i", host, port);

	outbead(sock, THIS_CODE, struct create_snapshot, snap);

	struct head head;
	unsigned maxbuf = 500;
	char buf[maxbuf];

	if ((err = readpipe(sock, &head, sizeof(head))))
		goto pipe_error;
	assert(head.length < maxbuf); // !!! don't die
	if ((err = readpipe(sock, buf, head.length)))
		goto pipe_error;
	trace_on(printf("reply = %x\n", head.code);)
	err  = head.code != THIS_REPLY;

	if (head.code == REPLY_ERROR)
		error("%.*s", head.length - 4, buf + 4);
pipe_error:
	close(sock);
	return err;
}
