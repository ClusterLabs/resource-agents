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


/* 
 * So what if we change this to a request chain like I've got in the
 * servers.  There are three lists. Free, Send, Reply.  Where the Reply
 * list is actually more of a hash.
 *
 * Activity goes:
 * - Grab struct from Free, malloc if needed.
 * - Stuff, stick on Send.
 * - Send, sends, then sticks on Reply.
 * - When handle_messages gets a reply, it looks up on Reply and handles
 *   from there.
 *
 * If Reply is a hash, it must be keyed on all important parts!
 * (keyname, subid, start, stop)
 *
 * uhh, this will have the same api as libgulm really.  It isn't really
 * anyhting more than a single thread for doing the sends.
 * And that begs the question, do I really need a thread just to do sends?
 *
 * I need something to avoid mutex starvation.
 *
 * Actually, these don't need to ever go into the Reply table unless they
 * are sync.
 *
 * am kinda thinking i should drop the send queue for a fifo mutex.  Still
 * doesn't deal with the receiving side yet thoguh.
 *
 *
 * I will need this structure in some form for handling replies.
 */
#ifndef __gulm_lockqueue_h__
#define __gulm_lockqueue_h__
#define glq_req_type_state  (1)
#define glq_req_type_action (2)
#define glq_req_type_drop   (3)
#define glq_req_type_cancel (4)
typedef struct glck_req {
	struct list_head list;

	/* these five for the key for hash-map look ups.
	* Any part of any of these can change and thus be a unique request.
	* (this struct is only put into a hash map to match replies.)
	*/
	uint8_t *key;
	uint16_t keylen;
	uint64_t subid;
	uint64_t start;
	uint64_t stop;

	/* other info about this request. */
	uint8_t type;
	uint8_t state;  /* also action */ /* changes on reply (anyflag) */
	uint32_t flags; /* changes on reply */
	uint8_t *lvb; /* changes on reply */
	uint16_t lvblen;
	uint32_t error;  /* changes on reply */

	/* when we get a reply, do this
	* this glck_req will not be on any list when finish is called.  Upon
	* the return of finish, it will be placed onto the Free list.
	*/
	void *misc;
	void (*finish)(struct glck_req *glck);

} glckr_t;

/* prototypes */
int glq_init(void);
int glq_startup(void);
void glq_shutdown(void);
void glq_release(void);
glckr_t *glq_get_new_req(void);
void glq_recycle_req(glckr_t *);
void glq_queue(glckr_t *);
void glq_cancel(glckr_t *);


#endif /*__gulm_lockqueue_h__*/
/* vim: set ai cin noet sw=8 ts=8 : */
