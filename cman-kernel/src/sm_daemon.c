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

#include "sm.h"

static unsigned long		daemon_flags;
static struct task_struct *	daemon_task;
static struct completion	daemon_done;
static wait_queue_head_t	daemon_wait;
extern int			sm_quorum;

void init_serviced(void)
{
	daemon_flags = 0;
	daemon_task = NULL;
	init_completion(&daemon_done);
	init_waitqueue_head(&daemon_wait);
}

void wake_serviced(int do_flag)
{
	set_bit(do_flag, &daemon_flags);
	wake_up(&daemon_wait);
}

static inline int got_work(void)
{
	int rv = 0;

	rv = (test_bit(DO_START_RECOVERY, &daemon_flags) ||
	      test_bit(DO_MESSAGES, &daemon_flags) ||
	      test_bit(DO_BARRIERS, &daemon_flags) ||
	      test_bit(DO_CALLBACKS, &daemon_flags));

	if (sm_quorum && !rv)
		rv = (test_bit(DO_JOINLEAVE, &daemon_flags) ||
		      test_bit(DO_RECOVERIES, &daemon_flags) ||
		      test_bit(DO_MEMBERSHIP, &daemon_flags));
	return rv;
}

static int serviced(void *arg)
{
	DECLARE_WAITQUEUE(wait, current);

	daemonize("cman_serviced");
	daemon_task = current;
	set_bit(DO_RUN, &daemon_flags);
	complete(&daemon_done);

	for (;;) {
		if (test_and_clear_bit(DO_START_RECOVERY, &daemon_flags))
			process_nodechange();

		if (test_and_clear_bit(DO_MESSAGES, &daemon_flags))
			process_messages();

		if (test_and_clear_bit(DO_BARRIERS, &daemon_flags))
			process_barriers();

		if (test_and_clear_bit(DO_CALLBACKS, &daemon_flags))
			process_callbacks();

		if (sm_quorum) {
			if (test_and_clear_bit(DO_RECOVERIES, &daemon_flags))
				process_recoveries();

			if (test_and_clear_bit(DO_JOINLEAVE, &daemon_flags))
				process_joinleave();

			if (test_and_clear_bit(DO_MEMBERSHIP, &daemon_flags))
				process_membership();
		}

		if (!test_bit(DO_RUN, &daemon_flags))
			break;

		current->state = TASK_INTERRUPTIBLE;
		add_wait_queue(&daemon_wait, &wait);
		if (!got_work() && test_bit(DO_RUN, &daemon_flags))
			schedule();
		remove_wait_queue(&daemon_wait, &wait);
		current->state = TASK_RUNNING;
	}

	complete(&daemon_done);
	return 0;
}

int start_serviced(void)
{
	int error;

	error = kernel_thread(serviced, NULL, 0);
	if (error < 0)
		goto out;

	error = 0;
	wait_for_completion(&daemon_done);

      out:
	return error;
}

void stop_serviced(void)
{
	clear_bit(DO_RUN, &daemon_flags);
	wake_up(&daemon_wait);
	wait_for_completion(&daemon_done);
}
