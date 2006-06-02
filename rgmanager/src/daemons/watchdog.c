/*
  Copyright Red Hat, Inc. 2005-2006

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
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/reboot.h>
#include <stdlib.h>

#include <signals.h>
#include <clulog.h>

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
				clulog(LOG_CRIT, "Watchdog: Daemon killed, exiting\n");
				raise(SIGKILL);
				while(1) ;
			}
			else {
#ifdef DEBUG
			        clulog(LOG_CRIT, "Watchdog: Daemon died, but not rebooting because DEBUG is set\n");
#else
				clulog(LOG_CRIT, "Watchdog: Daemon died, rebooting...\n");
				sync();
			        reboot(RB_AUTOBOOT);
#endif
				exit(255);
			}
		}
	}
}
