/******************************************************************************
*******************************************************************************
**
**  Copyright (C) Sistina Software, Inc.  1997-2003  All rights reserved.
**  Copyright (C) 2004 Red Hat, Inc.  All rights reserved.
**
**  This copyrighted material is made available to anyone wishing to use,
**  modify, copy, or redistribute it subject to the terms and conditions
**  of the GNU General Public License v.2.
**
*******************************************************************************
******************************************************************************/

#ifndef LINUX_GNBD_H
#define LINUX_GNBD_H

#define GNBD_DO_IT	_IO( 0xab, 0x20 )
#define GNBD_CLEAR_QUE	_IO( 0xab, 0x21 )
#define GNBD_PRINT_DEBUG	_IO( 0xab, 0x22 )
#define GNBD_DISCONNECT  _IO( 0xab, 0x23 )
#define GNBD_PING	_IO( 0xab, 0x24 )
#define GNBD_GET_TIME _IO( 0xab, 0x25 )

enum {
	GNBD_CMD_READ = 0,
	GNBD_CMD_WRITE = 1,
	GNBD_CMD_DISC = 2,
	GNBD_CMD_PING = 3
};

#define gnbd_cmd(req) ((req)->cmd[0])
#define MAX_GNBD 128

/* values for flags field */
#define GNBD_READ_ONLY 0x0001

/* userspace doesn't need the gnbd_device structure */
#ifdef __KERNEL__

struct gnbd_device {
	unsigned short int flags;
	struct socket * sock;
	struct file * file; 	/* If == NULL, device is not ready, yet	*/
	int magic;
	spinlock_t queue_lock;
	spinlock_t open_lock;
	struct list_head queue_head;/* Requests are added here...	*/
	struct semaphore tx_lock;
	struct gendisk *disk;
	pid_t receiver_pid;
	struct semaphore do_it_lock;
	int open_count;
	struct class_device class_dev;
	unsigned short int server_port;
	char *server_name;
	char name[32];
	unsigned long last_received;
	struct block_device *bdev;
};

#endif /* __KERNEL__ */

/* These are sent over the network in the request/reply magic fields */

#define GNBD_REQUEST_MAGIC 0x37a07e00
#define GNBD_REPLY_MAGIC 0x41f09370
#define GNBD_KEEP_ALIVE_MAGIC 0x5B46D8C2
/* Do *not* use magics: 0x12560953 0x96744668. */

/*
 * This is the packet used for communication between client and
 * server. All data are in network byte order.
 */
struct gnbd_request {
	uint32_t magic;
	uint32_t type;	/* == READ || == WRITE 	why so long */
	char handle[8];  /* why is this a char array instead of a u64 */
	uint64_t from;
	uint32_t len;
}
#ifdef __GNUC__
	__attribute__ ((packed))
#endif /* __GNUC__ */
;

/*
 * This is the reply packet that gnbd-server sends back to the client after
 * it has completed an I/O request (or an error occurs).
 */
#define SIZE_OF_REPLY 16
struct gnbd_reply {
	uint32_t magic;
	uint32_t error;		/* 0 = ok, else error	*/
	char handle[8];		/* handle you got from request	*/
};

struct do_it_req_s {
        unsigned int minor;
        int sock_fd;
};
typedef struct do_it_req_s do_it_req_t;

#endif /* LINUX_GNBD_H */
