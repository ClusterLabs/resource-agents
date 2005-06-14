#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h> 
#include <netinet/in.h>
#include <sys/poll.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <netdb.h> 
#include "ddsnap.h"
#include "../dm-ddsnap.h"
#include "trace.h"

#define trace trace_off

int open_socket(char *name, unsigned port)
{
	int sock;

	if ((sock = socket(AF_INET,  SOCK_STREAM, 0)) < 0)
		error("Can't get socket");

	struct hostent *host;
	if (!(host = gethostbyname(name)))
		error("Unknown host '%s'", name);

	struct sockaddr_in sockaddr = { .sin_family = AF_INET, .sin_port = htons(port) };
	memcpy(&sockaddr.sin_addr.s_addr, host->h_addr, host->h_length);
	if (connect(sock, (struct sockaddr *)&sockaddr, sizeof(sockaddr)) < 0) 
		error("Cannot connect to %s:%i", name, port);

	return sock;
}

#if 0
static unsigned seed = 0;

unsigned myrand(void)
{
	return seed = seed * 1871923741 + 3298913417U;
}
#else
#define myrand rand
#endif

int serviced = 0;

int incoming(unsigned sock)
{
	struct messagebuf message;
	int err, i, j;

	if ((err = readpipe(sock, &message.head, sizeof(message.head))))
		goto pipe_error;
	if (message.head.length > maxbody)
		goto message_too_long;
	trace(warn("%x+%u", message.head.code, message.head.length);)
	if ((err = readpipe(sock, &message.body, message.head.length)))
		goto pipe_error;

	switch (message.head.code) {
		case REPLY_ORIGIN_WRITE:
		{
			struct rw_request *body = (struct rw_request *)message.body;
			struct chunk_range *p = body->ranges;
			if (message.head.length < sizeof(*body))
				goto message_too_short;
			trace(printf("origin write reply, %u ranges ", body->count);)
			for (i = 0; i < body->count; i++, p++)
				trace(printf("%llu/%u ", p->chunk, p->chunks);)
			trace(printf("\n");)
			serviced++;
			break;
		}

		case REPLY_SNAPSHOT_WRITE:
		{
			struct rw_request *body = (struct rw_request *)message.body;
			struct chunk_range *p = body->ranges;
			if (message.head.length < sizeof(*body))
				goto message_too_short;
			trace(printf("snapshot write reply, %u ranges ", body->count);)
			for (i = 0; i < body->count; i++) {
				trace(printf("%llu/%u ", p->chunk, p->chunks);)
				chunk_t *q = (chunk_t *)(p + 1);
				for (j = 0; j < p->chunks; j++, q++)
					trace(printf("%llu ", *q));
				p = (struct chunk_range *)q;
			}
			trace(printf("\n");)
			serviced++;
			break;
		}

		case REPLY_CREATE_SNAPSHOT:
			trace(warn("create snapshot succeeded");)
			break;

		default: 
			warn("Unknown message %x", message.head.code);
	}
	return 0;

message_too_long:
	warn("message %x too long (%u bytes)\n", message.head.code, message.head.length);
	return -1;
message_too_short:
	warn("message %x too short (%u bytes)\n", message.head.code, message.head.length);
	return -1;
pipe_error:
	return -1;
}

unsigned available(unsigned sock)
{
	unsigned bytes;
	ioctl(sock, FIONREAD, &bytes);
	trace(if (bytes) printf("%u bytes waiting\n", bytes);)
	return bytes;
}

static unsigned total_chunks;

int main(int argc, char *argv[])
{
	int err, snapdev, orgdev, iterations = 1;

#if 0
	warn("rand = %u", myrand());
	warn("rand = %u", myrand());
	warn("rand = %u", myrand());
	warn("rand = %u", myrand());
	warn("rand = %u", myrand());
return 0;
#endif

	if (argc < 5)
		error("usage: %s dev/snapstore dev/origin hostname port [iterations]", argv[0]);
	if (!(snapdev = open(argv[1], O_RDWR /*| O_DIRECT*/)))
		error("Could not open snapshot store %s", argv[1]);
	if (!(orgdev = open(argv[2], O_RDWR /*| O_DIRECT*/)))
		error("Could not open origin volume %s", argv[2]);
	if (argc > 5)
		iterations = atoi(argv[5]);

	int sock = open_socket(argv[3], atoi(argv[4]));
	unsigned length_range = 32;
	unsigned chunk_range = (lseek(orgdev, 0, SEEK_END) >> 12) - length_range;

	outbead(sock, CREATE_SNAPSHOT, struct create_snapshot, 8);
	outbead(sock, CREATE_SNAPSHOT, struct create_snapshot, 9);
	outbead(sock, IDENTIFY, struct identify, .snap = -1);
	trace_on(warn("start %u transfers", iterations);)

	int i;
	for (i=0; i < iterations; i++) {
		unsigned length = offsetof(struct rw_request, ranges) + sizeof(struct chunk_range);
		struct { struct head head; struct rw_request body; char tail[maxbody]; } PACKED message;
		message.head.code = QUERY_WRITE;
		message.head.length = length;
 		message.body.count = 1;
		message.body.ranges[0].chunk = 0? (myrand() % chunk_range): total_chunks;
		message.body.ranges[0].chunks = 1;
		total_chunks += message.body.ranges[0].chunks = 0? (myrand() % length_range + 1): 1;

		if (write(sock, &message, sizeof(struct head) + length) < 0)
			error("Error writing to socket");

		while (available(sock) >= sizeof(struct head))
			incoming(sock);
	}
	while (serviced < iterations) {
		poll(NULL, 0, 100);
		trace(warn("wait for %i responses", iterations - serviced);)
		while (available(sock) >= sizeof(struct head))
			incoming(sock);
	}
//	outbead(sock, DUMP_TREE, struct { });
	close(sock);
	return err;
}
