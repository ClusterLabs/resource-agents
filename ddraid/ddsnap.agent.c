#include <stdio.h>
#include <stdlib.h>
#include <errno.h> 
#include <unistd.h> // read
#include <sys/socket.h>
#include <sys/poll.h>
#include <sys/un.h>
#include <netinet/in.h>
#include <libdlm.h>
#include "../dm-ddsnap.h" // message codes
#include "ddsnap.h" // outbead
#include "trace.h"
#include "sock.h" // send_fd, read/writepipe, connect_socket

#define trace trace_off

struct client { int sock; enum { CLIENT_CON, SERVER_CON } type; };

struct context {
	struct server active, local;
	int serv;
	int waiters; 
	struct client *waiting[100];
	struct dlm_lksb lksb;
	char lvb[DLM_LVB_LEN];
	int polldelay;
	unsigned ast_state;
};

static inline int have_address(struct server *server)
{
	return !!server->address_len;
}

int connect_clients(struct context *context)
{
	warn("connect clients to %x", *(int *)(context->active.address));
	while (context->waiters)
	{
		struct client *client = context->waiting[0];
		int control = client->sock;
		struct server *server = &context->active;
		struct sockaddr_in addr = { .sin_family = server->type, .sin_port = server->port };
		int sock;

		if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0)
			error("Can't get socket");
		memcpy(&addr.sin_addr.s_addr, server->address, server->address_len);
		if (connect(sock, (struct sockaddr *)&addr, sizeof(addr)) == -1) {
			warn("Can't connect to server, %s (%i)", strerror(errno), errno);
//			warn("try again later");
//			context->polldelay = 500;
			return -1;
		}
		if (outbead(control, CONNECT_SERVER, struct { }) < 0)
			error("Could not send connect message");
		if (send_fd(control, sock, "fark", 4) < 0)
			error("Could not pass server connection to target");
		context->waiting[0] = context->waiting[--context->waiters];
	}
	return 0;
}

#ifdef DLM
/*
 * Server instantiation algorithm using dlm+lvb:
 *
 * Repeat until bored:
 *    - Try to grab Protected Write without waiting
 *    - If we got it, write our server address to the lvb, start it, done
 *    - Otherwise, convert to Concurrent Read without waiting
 *    - If there is a server address in the lvb, use it, done
 *
 * Then punt to a human: somebody out there is sitting on the PW lock
 * but not distributing a server address.
 */

enum ast_state {
	dormant,
	read_lvb_done,
	write_lvb_done,
	check_write_lock,
};

#define LOCK "ddsnap" // !!! choose a sensible name and/or use a lockspace

void ast(void *arg); // bogus forward ref

void read_lvb(struct context *context, int flags) // bogus function
{
	struct dlm_lksb *lksb = &context->lksb;

	warn("read lvb");
	if (dlm_lock(LKM_CRMODE, lksb, flags|LKF_NOQUEUE|LKF_VALBLK, LOCK, strlen(LOCK), 0, ast, context, NULL, NULL))
		error("convert failed");
	context->ast_state = read_lvb_done;
}

void ast(void *arg)
{
	struct context *context = arg;
	struct dlm_lksb *lksb = &context->lksb;

	if (lksb->sb_status == EUNLOCK) {
		warn("released lock");
		memset(&context->active, 0, sizeof(struct server));
		return;
	}

	switch (context->ast_state) {
	case read_lvb_done:
		warn("read_lvb_done");
		if (lksb->sb_status)
			error("unexpected lock status (%i)", lksb->sb_status);
	
		if (have_address((struct server *)lksb->sb_lvbptr)) {
			memcpy(&context->active, lksb->sb_lvbptr, sizeof(struct server));
			connect_clients(context);
			return;
		}

		/* No address in lvb?  Sigh, we have to busywait. */
		context->ast_state = dormant;
		context->polldelay = 100;
		return;

	case write_lvb_done:
		warn("write_lvb_done");
		/* if this didn't work the dlm broken, might as well die */
		if (lksb->sb_status)
			error("unexpected lock status (%i)", lksb->sb_status);

		warn("Activate local server");
		memcpy(&context->active, &context->local, sizeof(struct server));
		if (outbead(context->serv, START_SERVER, struct { }) < 0)
			error("Could not send message to server");
		connect_clients(context);
		context->ast_state = dormant;
		return;

	case check_write_lock:
		warn("check_write_lock");
		warn("status = %i, %s", lksb->sb_status, strerror(lksb->sb_status));
		if (lksb->sb_status == EAGAIN) {
			/* We lost the race to start a server (probably) */
			read_lvb(context, 0);
			return;
		}

		warn("got write lock");
		memcpy(lksb->sb_lvbptr, &context->local, sizeof(struct server));
		if (dlm_lock(LKM_PWMODE, lksb, LKF_CONVERT|LKF_NOQUEUE|LKF_VALBLK, LOCK, strlen(LOCK), 0, ast, context, NULL, NULL))
			error("convert failed");
		context->ast_state = write_lvb_done;
		return;
	default:
		error("Bad ast state %i", context->ast_state);
	}
}

int try_to_instantiate(struct context *context)
{
	warn("Try to instantiate server");
	struct dlm_lksb *lksb = &context->lksb;
	if (dlm_lock(LKM_PWMODE, lksb, LKF_NOQUEUE, LOCK, strlen(LOCK), 0, ast, context, NULL, NULL)) {
		if (errno == EAGAIN) // bogus double handling of lock collision if master is local
			read_lvb(context, 0);
		else
			error("lock failed (%i) %s", errno, strerror(errno));
	} else
		context->ast_state = check_write_lock;
	return 0;
}

#else

int try_to_instantiate(struct context *context)
{
	error("Try to instantiate server");
	return 0;
}

#endif

int incoming(struct context *context, struct client *client)
{
	int err;
	struct messagebuf message;
	int sock = client->sock;

	if ((err = readpipe(sock, &message.head, sizeof(message.head))))
		goto pipe_error;
	if (message.head.length > maxbody)
		goto message_too_long;
	if ((err = readpipe(sock, &message.body, message.head.length)))
		goto pipe_error;

	switch (message.head.code) {
	case SERVER_READY:
		warn("received server ready");
		assert(message.head.length == sizeof(struct server));
		memcpy(&context->local, message.body, sizeof(struct server));
		context->serv = sock; // !!! refuse more than one
		client->type = SERVER_CON;
		goto instantiate;

	case NEED_SERVER:
		context->waiting[context->waiters++] = client;
		/*
		 * If we have a local server, try to instantiate it as the master.
		 * If there's already a master out there, connect to it.  If there
		 * was a master but it went away then the exclusive lock is up for
		 * grabs.  Always ensure the exclusive is still there before by
		 * trying to get it, before relying on the lvb server address,
		 * because that could be stale.
		 *
		 * If there's no local server, don't do anything: instantiation
		 * will be attempted when/if the local server shows up.
		 */
		if (have_address(&context->active)) {
			connect_clients(context);
			break;
		}
#ifdef DLM
		if (have_address(&context->local) && context->ast_state == dormant)
			goto instantiate;
#endif
		break;
	case REPLY_CONNECT_SERVER:
		warn("Everything connected properly, all is well");
		break;
	default: 
		warn("Unknown message %x", message.head.code);
		break;
	}
	return 0;

instantiate:
	return try_to_instantiate(context);

message_too_long:
	warn("message %x too long (%u bytes)\n", message.head.code, message.head.length);
pipe_error:
	return -1;
}

int monitor(char *sockname, struct context *context)
{
	unsigned maxclients = 100, clients = 0, others = 2;
	struct pollfd pollvec[others+maxclients];
	struct client *clientvec[maxclients];
	struct sockaddr_un addr = { .sun_family = AF_UNIX };
	int addr_len = sizeof(addr) - sizeof(addr.sun_path) + strlen(sockname);
	int listener = socket(AF_UNIX, SOCK_STREAM, 0), locksock;

	assert(listener > 0);
	strncpy(addr.sun_path, sockname, sizeof(addr.sun_path));
	if (sockname[0] == '@')
		addr.sun_path[0] = 0;
	else
		unlink(sockname);

	if (bind(listener, (struct sockaddr *)&addr, addr_len) || listen(listener, 5))
		error("Can't bind to control socket (is it in use?)");

#ifdef DLM
	/* Set up lock manager */
	if ((locksock = dlm_get_fd()) < 0)
		error("dlm error %i, %s", errno, strerror(errno));
	context->lksb.sb_lvbptr = context->lvb; /* Yuck! */
	memset(context->lvb, 0, sizeof(context->lvb));
#endif

	/* Launch daemon and exit */
	switch (fork()) {
	case -1:
		error("fork failed");
	case 0:
		break; // !!! should daemonize properly
	default:
		return 0;
	}

	pollvec[0] = (struct pollfd){ .fd = listener, .events = POLLIN };
	pollvec[1] = (struct pollfd){ .fd = locksock, .events = POLLIN };
	assert(pollvec[0].fd > 0);

	while (1) {
		switch (poll(pollvec, others+clients, context->polldelay)) {
		case -1:
			if (errno == EINTR)
				continue;
			error("poll failed, %s", strerror(errno));
		case 0:
			/* Timeouts happen here */
			context->polldelay = -1;
			warn("try again");
			connect_clients(context);
			// If we go through this too many times it means somebody
			// out there is sitting on the PW lock but did not write
			// the lvb, this is breakage that should be reported to a
			// human.  So we should do that, but also keep looping
			// forever in case somebody is just being slow or in the
			// process of being fenced/ejected, in which case the PW
			// will eventually come free again.  Yes this sucks.
			continue;
		}

		/* New connection? */
		if (pollvec[0].revents) {
			struct sockaddr_in addr;
			int addr_len = sizeof(addr), sock;

			if (!(sock = accept(listener, (struct sockaddr *)&addr, &addr_len)))
				error("Cannot accept connection");
			trace_on(warn("Received connection %i", clients);)
			assert(clients < maxclients); // !!! make the array bigger

			struct client *client = malloc(sizeof(struct client));
			*client = (struct client){ .sock = sock };
			clientvec[clients] = client;
			pollvec[others+clients] = (struct pollfd){ .fd = sock, .events = POLLIN };
			clients++;
		}
#ifdef DLM
		/* Lock event? */
		if (pollvec[1].revents)
			dlm_dispatch(locksock);
#endif
		/* Activity on connection? */
		unsigned i = 0;
		while (i < clients) {
			if (pollvec[others+i].revents) { // !!! check for poll error
				struct client **clientp = clientvec + i, *client = *clientp;

				if (incoming(context, client) == -1) {
					warn("Lost connection %i", i);
					if (client->type == SERVER_CON) {
						warn("local server died...");
						if (!memcmp(&context->active, &context->local, sizeof(struct server))) {
							warn("release lock");
							struct dlm_lksb *lksb = &context->lksb;
							memset(&context->active, 0, sizeof(struct server));
							memset(lksb->sb_lvbptr, 0, sizeof(struct server));
#ifdef DLM
						        if (dlm_unlock(lksb->sb_lkid, 0, lksb, context) < 0)
		 						warn("dlm error %i, %s", errno, strerror(errno));
#endif
						}
						memset(&context->local, 0, sizeof(struct server));
					}
					close(client->sock);
					free(client);
					--clients;
					clientvec[i] = clientvec[clients];
					pollvec[others + i] = pollvec[others + clients];
//					memmove(clientp, clientp + 1, sizeof(struct client *) * clients);
//					memmove(pollvec + i + others, pollvec + i + others + 1, sizeof(struct pollfd) * clients);
					continue;
				}
			}
			i++;
		}
	}
}

int main(int argc, char *argv[])
{
	if (argc != 2)
		error("usage: %s sockname", argv[0]);

	return monitor(argv[1], &(struct context){ .polldelay = -1 });
}
