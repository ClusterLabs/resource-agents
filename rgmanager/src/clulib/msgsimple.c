/*
  Copyright Red Hat, Inc. 2002-2003
  Copyright Mission Critical Linux, 2000

  This program is free software; you can redistribute it and/or modify it
  under the terms of the GNU General Public License as published by the
  Free Software Foundation; either version 2, or (at your option) any
  later version.

  This program is distributed in the hope that it will be useful, but
  WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; see the file COPYING.  If not, write to the
  Free Software Foundation, Inc.,  675 Mass Ave, Cambridge, 
  MA 02139, USA.
*/
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/errno.h>
#include <sys/socket.h>
#include <malloc.h>
#include <linux/limits.h>
#include <sys/time.h>
#include <sys/un.h>
#include <dirent.h>
#include <string.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <libgen.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <fcntl.h>
#include <sys/syslog.h>
#include <sys/ioctl.h>

#include <vf.h>
#include <magmamsg.h>
#include <platform.h>

#include <mallocdbg.h>

ssize_t msg_peek(int sockfd, void *buf, ssize_t count);

/*
 * msg_send_simple
 */
int
msg_send_simple(int fd, int cmd, int arg1, int arg2)
{
	generic_msg_hdr msg;

	msg.gh_magic = GENERIC_HDR_MAGIC;
	msg.gh_length = sizeof (msg);
	msg.gh_command = cmd;
	msg.gh_arg1 = arg1;
	msg.gh_arg2 = arg2;
	swab_generic_msg_hdr(&msg);

	return msg_send(fd, (void *) &msg, sizeof (msg));
}


/*
 * receive_message
 *
 * Read a message and the corresponding buffer off of the file descriptor
 * indicated by fd.  This allocates **buf; so the user must free it when 
 * [s]he is done with it.  Also returns the length of the full buffer in
 * *buf_size.
 *
 * Returns 0 on success or -1 on failure.
 */
int
msg_receive_simple(int fd, generic_msg_hdr ** buf, int timeout)
{
	int ret;
	generic_msg_hdr peek_msg;

	/*
	 * Peek at the header.  We need the size of the inbound buffer!
	 */
	ret = msg_peek(fd, &peek_msg, sizeof (generic_msg_hdr));
	if (ret != sizeof (generic_msg_hdr)) {
		if (ret == -1) {
			if (errno != ECONNRESET)
				fprintf(stderr, "fd%d peek: %s\n", fd,
					strerror(errno));
		} else if (ret != 0)	/* Blank message = probably closed socket */
			fprintf(stderr, "fd%d peek: %d/%d bytes\n", fd,
			       ret, sizeof (generic_msg_hdr));
		return -1;
	}

	swab_generic_msg_hdr(&peek_msg);
	if (peek_msg.gh_magic != GENERIC_HDR_MAGIC) {
		fprintf(stderr, "Invalid magic: Wanted 0x%08x, got 0x%08x\n",
		       GENERIC_HDR_MAGIC, peek_msg.gh_magic);
		return -1;
	}

	/* 
	 * Ensure someone didn't just try to crash us
	 */
	if (peek_msg.gh_length > MSG_MAX_SIZE) {
		fprintf(stderr, "Message rejected: Maximum size exceeded!\n");
		return -1;
	}

	/*
	 * allocate enough memory to receive the header + diff buffer
	 */
	*buf = malloc(peek_msg.gh_length);
	memset(*buf, 0, peek_msg.gh_length);

	if (!*buf) {
		fprintf(stderr, "%s: malloc: %s", __FUNCTION__,
		       strerror(errno));
		return -1;
	}

	/*
	 * Now, do the real receive.  2 second timeout, if none specified.
	 */
	ret = msg_receive_timeout(fd, (void *) (*buf), peek_msg.gh_length,
				  timeout ? timeout : 2);

	if (ret == -1) {
		fprintf(stderr, "msg_receive_timeout: %s\n", strerror(errno));
		free(*buf);
		*buf = NULL;
		return -1;
	}

	if (ret != peek_msg.gh_length) {
		fprintf(stderr, "short read: %d/%d\n", ret, peek_msg.gh_length);
		free(*buf);
		*buf = NULL;
		return -1;
	}

	return ret;
}
