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
extern int			sm_quorum;

void init_serviced(void)
{
	daemon_flags = 0;
	daemon_task = NULL;
}

void wake_serviced(int do_flag)
{
	set_bit(do_flag, &daemon_flags);
	wake_up_process(daemon_task);
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
	while (!kthread_should_stop()) {
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

		set_current_state(TASK_INTERRUPTIBLE);
		if (!got_work())
			schedule();
		set_current_state(TASK_RUNNING);
	}

	return 0;
}

int start_serviced(void)
{
	struct task_struct *p;

	p = kthread_run(serviced, NULL, "cman_serviced");
	if (IS_ERR(p)) {
		printk("can't start cman_serviced daemon");
		return (IS_ERR(p));
	}

	daemon_task = p;
	return 0;
}

void stop_serviced(void)
{
	kthread_stop(daemon_task);
}
