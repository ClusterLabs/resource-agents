/*
  Copyright Red Hat, Inc. 2003

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


#include <signal.h>
#include <stdlib.h>
#include <string.h>
//#include <resgroup.h>

#include "signals.h"

void *
setup_signal(int sig, void (*handler)(int))
{
	struct sigaction act;
	struct sigaction oldact;

	memset(&act, 0, sizeof(act));
	act.sa_handler = handler;

	unblock_signal(sig);
	if (sigaction(sig, &act, &oldact) == 0) {
		return oldact.sa_handler;
	}

	return NULL;
}


/**
 * Block the given signal.
 *
 * @param sig		Signal to block.
 * @return		See man sigprocmask.
 */
int
block_signal(int sig)
{
       	sigset_t set;

	sigemptyset(&set);
	sigaddset(&set, sig);
	
	return(sigprocmask(SIG_BLOCK, &set, NULL));
}


/**
 * Block the given signal.
 *
 * @param sig		Signal to block.
 * @return		See man sigprocmask.
 */
int
unblock_signal(int sig)
{
       	sigset_t set;

	sigemptyset(&set);
	sigaddset(&set, sig);
	
	return(sigprocmask(SIG_UNBLOCK, &set, NULL));
}


int
block_all_signals(void)
{
       	sigset_t set;

	sigfillset(&set);
	sigdelset(&set, SIGSEGV);
	return(sigprocmask(SIG_BLOCK, &set, NULL));
}
