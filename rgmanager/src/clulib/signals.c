#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <resgroup.h>

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
