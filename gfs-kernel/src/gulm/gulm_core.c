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

#include "gulm.h"

#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/slab.h>
#include <linux/file.h>
#define __KERNEL_SYSCALLS__
#include <linux/unistd.h>

#include "util.h"
#include "utils_tostr.h"

extern gulm_cm_t gulm_cm;

/* private vars. */
int cm_thd_running;
struct completion cm_thd_startup;
struct task_struct *cm_thd_task;

/**
 */
int
gulm_core_login_reply (void *misc, uint64_t gen, uint32_t error,
		       uint32_t rank, uint8_t corestate)
{
	if (error != 0) {
		log_err ("Core returned error %d:%s.\n", error,
			 gio_Err_to_str (error));
		cm_thd_running = FALSE;
		return error;
	}

	if( gulm_cm.GenerationID != 0 ) {
		GULM_ASSERT(gulm_cm.GenerationID == gen,
				printk("us: %"PRIu64" them: %"PRIu64"\n",
					gulm_cm.GenerationID,gen);
				);
	}
	gulm_cm.GenerationID = gen;

	error = lt_login ();
	if (error != 0) {
		log_err ("lt_login failed. %d\n", error);
		lg_core_logout (gulm_cm.hookup);	/* XXX is this safe? */
		return error;
	}

	log_msg (lgm_Network2, "Logged into local core.\n");

	return 0;
}

/**
 * gulm_core_logout_reply - 
 * @misc: 
 * 
 * 
 * Returns: int
 */
int
gulm_core_logout_reply (void *misc)
{
	log_msg (lgm_Network2, "Logged out of local core.\n");
	return 0;
}

/**
 */
int
gulm_core_nodechange (void *misc, char *nodename,
		      struct in6_addr *nodeip, uint8_t nodestate)
{
	if (nodestate == lg_core_Fenced) {
		request_journal_replay (nodename);
	}
	/* if me and state is logout, Need to close out things if we can.
	 */
	if (gulm_cm.starts && nodestate == lg_core_Logged_out &&
			strcmp(gulm_cm.myName, nodename) == 0 ) {
		lt_logout();
		cm_thd_running = FALSE;
		lg_core_logout (gulm_cm.hookup);
		return -1;
	}
	return 0;
}

int gulm_core_statechange (void *misc, uint8_t corestate,
                           struct in6_addr *masterip, char *mastername)
{
	int *cst = (int *)misc;
	if( misc != NULL ) {
		if( corestate != lg_core_Slave &&
				corestate != lg_core_Master ) {
			*cst = TRUE;
		}else{
			*cst = FALSE;
		}
	}
	return 0;
}

/**
 */
int
gulm_core_error (void *misc, uint32_t err)
{
	log_err ("Got error code %d %#x back fome some reason!\n", err, err);
	return 0;
}

static lg_core_callbacks_t core_cb = {
      login_reply:gulm_core_login_reply,
      logout_reply:gulm_core_logout_reply,
      nodechange:gulm_core_nodechange,
      statechange:gulm_core_statechange,
      error:gulm_core_error
};

/**
 * cm_io_recving_thread - 
 * @data: 
 * 
 * 
 * Returns: int
 */
int
cm_io_recving_thread (void *data)
{
	int err;

	daemonize ("gulm_res_recvd");
	cm_thd_task = current;
	complete (&cm_thd_startup);

	while (cm_thd_running) {
		err = lg_core_handle_messages (gulm_cm.hookup, &core_cb, NULL);
		if (err != 0) {
			log_err
			    ("Got an error in gulm_res_recvd err: %d\n", err);
			if (!cm_thd_running)
				break;
			/* 
			 * Pause a bit, then try to log back into the local
			 * lock_gulmd.  Keep doing this until an outside force
			 * stops us. (which I don't think there is any at this
			 * point.  forceunmount would be one, if we ever do
			 * that.)
			 *
			 * If we are still in the gulm_mount() function, we
			 * should not retry. We should just exit.
			 */
			current->state = TASK_INTERRUPTIBLE;
			schedule_timeout (3 * HZ);

			while ((err =
				lg_core_login (gulm_cm.hookup, TRUE)) != 0) {
				log_err
				    ("Got a %d trying to login to lock_gulmd.  Is it running?\n",
				     err);
				current->state = TASK_INTERRUPTIBLE;
				schedule_timeout (3 * HZ);
			}
		}
	}			/* while( gulm_cm.cm_thd_running ) */

	complete (&cm_thd_startup);
	return 0;
}

/**
 * cm_logout - 
 */
void
cm_logout (void)
{

	if (cm_thd_running) {
		cm_thd_running = FALSE;
		lg_core_logout (gulm_cm.hookup);

		/* wait for thread to finish */
		wait_for_completion (&cm_thd_startup);
	}

}

/**
 * cm_login - 
 * 
 * Returns: int
 */
int
cm_login (void)
{
	int err = -1;
	int cst=TRUE;

	cm_thd_running = FALSE;
	init_completion (&cm_thd_startup);

	err = lg_core_login (gulm_cm.hookup, TRUE);
	if (err != 0) {
		log_err
		    ("Got a %d trying to login to lock_gulmd.  Is it running?\n",
		     err);
		goto exit;
	}
	/* handle login reply.  which will start the lt thread. */
	err = lg_core_handle_messages (gulm_cm.hookup, &core_cb, NULL);
	if (err != 0) {
		goto exit;
	}

	/* do not pass go until Slave(client) or Master */
	while(cst) {
		lg_core_corestate(gulm_cm.hookup);
		err = lg_core_handle_messages (gulm_cm.hookup, &core_cb, &cst);
		if (err != 0) {
			goto exit;
		}
		if(cst) {
			current->state = TASK_INTERRUPTIBLE;
			schedule_timeout (3 * HZ);
			/* if interrupted, exit */
		}
	}

	/* start recver thread. */
	cm_thd_running = TRUE;
	err = kernel_thread (cm_io_recving_thread, NULL, 0);
	if (err < 0) {
		log_err ("Failed to start gulm_res_recvd. (%d)\n", err);
		goto exit;
	}
	wait_for_completion (&cm_thd_startup);

	err = 0;
      exit:
	return err;
}
/* vim: set ai cin noet sw=8 ts=8 : */
