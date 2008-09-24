#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/reboot.h>
#include <stdlib.h>

#include <signals.h>
#include <logging.h>

static pid_t child = 0;

static void 
signal_handler(int signum)
{
        kill(child, signum);
}
static void 
redirect_signals(void)
{
        int i;
        for (i = 0; i < _NSIG; i++) {
	        switch (i) {
		case SIGCHLD:
		case SIGILL:
		case SIGFPE:
		case SIGSEGV:
		case SIGBUS:
		        setup_signal(i, SIG_DFL);
			break;
		default:
		        setup_signal(i, signal_handler);
		}
	}
}

/**
 return watchdog's pid, or 0 on failure
*/
int 
watchdog_init(void)
{
	int status;
	pid_t parent;
	
	parent = getpid();
	child = fork();
	if (child < 0)
	        return 0;
	else if (!child)
		return parent;
	
	redirect_signals();
	
	while (1) {
	        if (waitpid(child, &status, 0) <= 0)
		        continue;
		
		if (WIFEXITED(status))
		        exit(WEXITSTATUS(status));
		
		if (WIFSIGNALED(status)) {
		        if (WTERMSIG(status) == SIGKILL) {
				log_printf(LOG_CRIT, "Watchdog: Daemon killed, exiting\n");
				raise(SIGKILL);
				while(1) ;
			}
			else {
#ifdef DEBUG
			        log_printf(LOG_CRIT, "Watchdog: Daemon died, but not rebooting because DEBUG is set\n");
#else
				log_printf(LOG_CRIT, "Watchdog: Daemon died, rebooting...\n");
				sync();
			        reboot(RB_AUTOBOOT);
#endif
				exit(255);
			}
		}
	}
}
