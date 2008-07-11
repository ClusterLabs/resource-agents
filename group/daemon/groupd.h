#ifndef __GROUPD_DOT_H__
#define __GROUPD_DOT_H__

#define GROUPD_SOCK_PATH        ("groupd_socket")
#define NODE_FAILED		(1)
#define NODE_JOIN		(2)
#define NODE_LEAVE		(3)
#define GROUPD_MSGLEN		(2200) /* should be enough to permit
					  group_send() of up to 2048 bytes
					  of data plus header info */
#define GROUPD_DUMP_SIZE	(1024 * 1024)

#endif
