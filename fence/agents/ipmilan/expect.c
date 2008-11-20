/** @file
 * Simple expect module for the STONITH library.
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <string.h>
#include <errno.h>
#include <syslog.h>
#include <libintl.h>
#include <sys/wait.h>
#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include <time.h>
#include <sys/time.h>
#include <sys/times.h>
#ifdef _POSIX_PRIORITY_SCHEDULING
#	include <sched.h>
#endif

#include "expect.h"

#ifndef EOS
#	define	EOS '\0'
#endif


/* 
 * Just incase we are on an out of date system 
 */
#ifndef CLOCKS_PER_SEC
#  ifndef CLK_TCK
#    error Neither CLOCKS_PER_SEC nor CLK_TCK (obsolete) are defined
#  endif /* CLK_TCK */
#  define CLOCKS_PER_SEC CLK_TCK
#endif /* CLOCKS_PER_SEC */


void close_all_files (void);

/**
 * Look for ('expect') any of a series of tokens in the input
 * Return the token type for the given token or -1 on error.
 *
 * @param fd		The file descriptor to watch.
 * @param toklist	The series of tokens to look for.
 * @param to_secs	Timeout (in seconds).
 * @param buf		Receive buffer (preallocated).
 * @param maxline	Length of receive buffer.
 */
int
ExpectToken(int	fd, struct Etoken * toklist, int to_secs, char * buf
,	int maxline)
{
	clock_t		starttime;
	clock_t		endtime;
	int		wraparound=0;
	int		tickstousec = (1000000/CLOCKS_PER_SEC);
	clock_t		now;
	clock_t		ticks;
	int		nchars = 1; /* reserve space for an EOS */
	struct timeval	tv;

	struct Etoken *	this;

	/* Figure out when to give up.  Handle lbolt wraparound */
	if (fd < 0) {
		errno = EINVAL;
		return -1;
	}
	
	starttime = times(NULL);
	ticks = (to_secs*CLOCKS_PER_SEC);
	endtime = starttime + ticks;

	if (endtime < starttime) {
		wraparound = 1;
	}

	if (buf) {
		*buf = EOS;
	}

	for (this=toklist; this->string; ++this) {
		this->matchto = 0;
	}


	while (now = times(NULL),
		(wraparound && (now > starttime || now <= endtime))
		||	(!wraparound && now <= endtime)) {

		fd_set infds;
		char	ch;
		clock_t		timeleft;
		int		retval;

		timeleft = endtime - now;

		tv.tv_sec = timeleft / CLOCKS_PER_SEC;
		tv.tv_usec = (timeleft % CLOCKS_PER_SEC) * tickstousec;

		if (tv.tv_sec == 0 && tv.tv_usec < tickstousec) {
			/* Give 'em a little chance */
			tv.tv_usec = tickstousec;
		}

		/* Watch our FD to see when it has input. */
           	FD_ZERO(&infds);
           	FD_SET(fd, &infds);

		retval = select(fd+1, &infds, NULL, NULL, &tv); 
		if (retval <= 0) {
			errno = ETIMEDOUT;
			return(-1);
		}
		/* Whew!  All that work just to read one character! */
		
		if (read(fd, &ch, sizeof(ch)) <= 0) {
			return(-1);
		}
		/* Save the text, if we can */
		if (buf && nchars < maxline-1) {
			*buf = ch;
			++buf;
			*buf = EOS;
			++nchars;
		}
#if 0
		fprintf(stderr, "%c", ch);
#endif

		/* See how this character matches our expect strings */

		for (this=toklist; this->string; ++this) {

			if (ch == this->string[this->matchto]) {

				/* It matches the current token */

			 	++this->matchto;
				if (this->string[this->matchto] == EOS){
					/* Hallelujah! We matched */
					return(this->toktype);
				}
			}else{

				/* It doesn't appear to match this token */

				int	curlen;
				int	nomatch=1;
				/*
				 * If we already had a match (matchto is
				 * greater than zero), we look for a match
				 * of the tail of the pattern matched so far
				 * (with the current character) against the
				 * head of the pattern.
				 */

				/*
				 * This is to make the string "aab" match
				 * the pattern "ab" correctly 
				 * Painful, but nice to do it right.
				 */

				for (curlen = (this->matchto)
				;	nomatch && curlen >= 0
				;	--curlen) 			{
					const char *	tail;
					tail=(this->string)
					+	this->matchto
					-	curlen;

					if (strncmp(this->string, tail
					,	curlen) == 0
					&&	this->string[curlen] == ch)  {
						
						if (this->string[curlen+1]==EOS){
							/* We matched!  */
							/* (can't happen?) */
							return(this->toktype);
						}
						this->matchto = curlen+1;
						nomatch=0;
					}
				}
				if (nomatch) {
					this->matchto = 0;
				}
			}
		}
	}
	errno = ETIMEDOUT;
	return(-1);
}

/**
 * Start a process with its stdin and stdout redirected to pipes
 * so the parent process can talk to it.
 *
 * @param cmd		Command line to run.
 * @param readfd	Filled with a pipe to the the output of the
 *			new child.
 * @param writefd	Filled with a pipe to the input of the new
 *			child.
 * @param redir_err	0, 1 = stderr, 2 = setsid, 3 = both
 * @return		-1 on failure (with errno set appropriately) or
 *			or the PID of the new child process.
 */
int
StartProcess(const char * cmd, int * readfd, int * writefd, int flags)
{
	pid_t	pid;
	int	wrpipe[2];	/* The pipe the parent process writes to */
				/* (which the child process reads from) */
	int	rdpipe[2];	/* The pipe the parent process reads from */
				/* (which the child process writes to) */

	if (pipe(wrpipe) < 0) {
		perror("cannot create pipe\n");
		return(-1);
	}
	if (pipe(rdpipe) < 0) {
		perror("cannot create pipe\n");
		close(wrpipe[0]);
		close(wrpipe[1]);
		return(-1);
	}
	switch(pid=fork()) {
	case -1:	
		perror("cannot StartProcess cmd");
		close(rdpipe[0]);
		close(rdpipe[1]);
		close(wrpipe[0]);
		close(wrpipe[1]);
		return(-1);

	case 0:
		/* We are the child */
		/* Redirect stdin */
		if (wrpipe[0] != 0) {
			close(0);
			if(dup2(wrpipe[0], 0) < 0) {
			    syslog(LOG_CRIT,
			    	   "StartProcess: dup2(%d,0) failed: %s\n",
			    	   wrpipe[0],
			    	   strerror(errno));
			    exit(1);
			}
			close(wrpipe[0]);
		}
		close(wrpipe[1]);

		/* Redirect stdout */
		if (rdpipe[1] != 1) {
			close(1);
			if(dup2(rdpipe[1], 1) < 0) {
			    syslog(LOG_CRIT,
			    	   "StartProcess: dup2(%d,1) failed: %s\n",
			    	   rdpipe[1],
			    	   strerror(errno));
			    exit(1);
			}
			close(rdpipe[1]);
		}
		close(rdpipe[0]);
		
		if (flags & EXP_STDERR) {
			/* Redirect stderr */
			close(2);
			if(dup2(1, 2) < 0) {
				syslog(LOG_CRIT,
				       "StartProcess: dup2(1,2) failed: %s\n",
				       strerror(errno));
				exit(1);
			}
		}
		
		if (flags & EXP_NOCTTY)
			setsid();
		close_all_files(); /* Workaround telnet bugs */
#if defined(SCHED_OTHER)
		{
			/*
			 * Try and (re)set our scheduling to "normal"
			 * Sometimes our callers run in soft
			 * real-time mode.  The program we exec might
			 * not be very well behaved - this is bad for
			 * operation in high-priority (soft real-time)
			 * mode.  In particular, telnet is prone to
			 * going into infinite loops when killed.
			 */
			struct sched_param	sp;
			memset(&sp, 0, sizeof(sp));
			sp.sched_priority = 0;
			sched_setscheduler(0, SCHED_OTHER, &sp);
		}
#endif
		execlp("/bin/bash", "bash", "-c", cmd, NULL);
		perror("cannot exec shell!");
		exit(1);

	default:	/* We are the parent */
		*readfd = rdpipe[0];
		close(rdpipe[1]);
		*writefd = wrpipe[1];
		close(wrpipe[0]);
		return(pid);
	}
	/*NOTREACHED*/
	return(-1);
}

/**
 * Close all file descriptors in a child process.
 *
 * Open fd's are inherited across exec unless they are
 * marked close on exec, which must be done explicitly
 * with fcntl().  While this should not affect the operation of
 * telnet, it was found that in some cases it did.  Its easier to
 * just fix it this way than to fix telnet.
 */
void
close_all_files (void)
{
	register int i, fd_table_size;

	fd_table_size = getdtablesize ();
	if (fd_table_size > 256)/* clamp to a reasonable value */
		fd_table_size = 256;
	
	for (i = 3; i < fd_table_size; i++)
		close (i);
}
