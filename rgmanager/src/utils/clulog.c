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
#include <corosync/engine/logsys.h>
#include <ccs.h>
#include <logging.h>

void
usage(char *progname)
{
	fprintf(stdout, "%s -s severity \"message text\"\n", progname);
	exit(0);
}


int
main(int argc, char **argv)
{
	int opt, ccsfd;
	int severity = -1;

	char *logmsg = argv[argc-1];

	while ((opt = getopt(argc, argv, "s:h")) != EOF) {
		switch(opt) {
		case 's':
			severity = atoi(optarg);
			break;
		case 'h':
		default:
			usage(argv[0]);
			break;
		}
	}

	if (severity < 0)
		severity = SYSLOGLEVEL;

	init_logging(1, severity);
	ccsfd = ccs_connect();
	setup_logging(ccsfd);
	ccs_disconnect(ccsfd);

	log_printf(severity, "%s", logmsg);

	close_logging();
	return 0;
}
