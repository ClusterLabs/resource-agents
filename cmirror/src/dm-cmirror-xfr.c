/*
 * Copyright (C) 2005 Red Hat, Inc.
 *
 * This file is released under the LGPL.
 */

#include <linux/module.h>
#include <linux/net.h>
#include <linux/in.h>
#include <linux/socket.h>
#include <linux/timer.h>
#include <linux/signal.h>


static void set_sigusr1(unsigned long arg){
        struct task_struct *tsk = (struct task_struct *)arg;
        send_sig(SIGUSR1, tsk, 0);
}

int my_recvmsg(struct socket *sock, struct msghdr *msg,
	       size_t size, int flags, int time_out){
	int rtn;
	unsigned long sig_flags;
	sigset_t blocked_save;
	struct timer_list timer = TIMER_INITIALIZER(set_sigusr1,
						    jiffies+(time_out*HZ),
						    (unsigned long)current);

        spin_lock_irqsave(&current->sighand->siglock, sig_flags);
        blocked_save = current->blocked;
        sigdelsetmask(&current->blocked, sigmask(SIGUSR1));
        recalc_sigpending();
        spin_unlock_irqrestore(&current->sighand->siglock, sig_flags);

	add_timer(&timer);
	rtn = sock_recvmsg(sock, msg, size, flags);
	del_timer(&timer);

	/* flush after recalc?  Redo this function.  */
	spin_lock_irqsave(&current->sighand->siglock, sig_flags);
        current->blocked = blocked_save;
        recalc_sigpending();
        spin_unlock_irqrestore(&current->sighand->siglock, sig_flags);
        flush_signals(current);

	if(rtn < 0){
		return -ETIMEDOUT;  /* perhaps not the best error number */
	}
	return rtn;
}
