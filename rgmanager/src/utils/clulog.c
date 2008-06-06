/** @file
 * Utility for logging arbitrary strings to the cluster log file via syslog.
 *
 * Author: Jeff Moyer <jmoyer at redhat.com>
 */
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <getopt.h>
#include <string.h>
#include <sys/syslog.h>
#include <clulog.h>
#include <ccs.h>

int configure_logging(int);

#define MAX_TOKENLEN   64

void
usage(char *progname)
{
    fprintf(stdout, "%s -s severity [-f facility] [-l priority_filter] [-n program name] \n"
	    "\t\t [-p pid] \"message text\"\n", progname);
    exit(0);
}


/*
 * Configure logging based on data in cluster.conf
 */
int
configure_logging(int ccsfd)
{
	char *v;
	char internal = 0;

	if (ccsfd < 0) {
		internal = 1;
		ccsfd = ccs_connect();
		if (ccsfd < 0)
			return -1;
	}

	if (ccs_get(ccsfd, "/cluster/rm/@log_facility", &v) == 0) {
		clu_set_facility(v);
		free(v);
	}

	if (ccs_get(ccsfd, "/cluster/rm/@log_level", &v) == 0) {
		clu_set_loglevel(atoi(v));
		free(v);
	}

	if (internal)
		ccs_disconnect(ccsfd);

	return 0;
}


int
main(int argc, char **argv)
{
    int  opt;
    int  severity = 7,
	 cmdline_loglevel = 0;/* set if we should not use the config file val*/
    char *logmsg;
    int  pid = 0;
    char *progname = NULL;
    int result;
    size_t len;

    if (argc < 4)
	usage(argv[0]);

    while ((opt = getopt(argc, argv, "f:l:s:hp:n:")) != -1) {
	switch (opt) {
	case 'l':
	    clu_set_loglevel(atoi(optarg));
	    cmdline_loglevel = 1;
	case 'f':
	    clu_set_facility(optarg);
	    break;
	case 's':
	    severity = atoi(optarg);
	    break;
	case 'p':
	    pid = atoi(optarg);
	    break;
	case 'n':
	    progname = strdup(optarg);
	    break;
	case 'h':
	    usage(argv[0]);
	default:
	    usage(argv[0]);
	}
    }

    /* Add two bytes for linefeed and NULL terminator */
    len = strlen(argv[argc-1]) + 2;
    logmsg = (char*)malloc(strlen(argv[argc-1])+2);
    if (logmsg == NULL) {
        fprintf(stderr,
            "clulog: malloc fail err=%d\n", errno);
        exit(0);
    }

    snprintf(logmsg, len, "%s\n", argv[argc-1]);

    if (!cmdline_loglevel) {
	/*
	 * Let's see what loglevel the SM is running at.
	 * If ccsd's not available, use default.
	 */
    	if (configure_logging(-1) < 0)
		clu_set_loglevel(LOGLEVEL_DFLT);
    }
    result = clulog_pid(severity, pid, progname, logmsg);
    free(progname);
    return(result);
}
