/** @file
 * Utility for logging arbitrary strings to the cluster log file via syslog.
 *
 * Author: Lon Hohberger <lhh at redhat.com>
 * Based on original code by: Jeff Moyer <jmoyer at redhat.com>
 */
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/types.h>
#include <unistd.h>
#include <limits.h>
#include <libgen.h>
#include <getopt.h>
#include <string.h>
#include <ccs.h>
#include <logging.h>

void
usage(char *progname)
{
	fprintf(stdout, "%s -s severity \"message text\"\n", progname);
	exit(0);
}


char *
log_name(void)
{
	char lnk[PATH_MAX];
	static char file[PATH_MAX];

	snprintf(lnk, sizeof(lnk), "/proc/%d/exe", getppid());

	printf("%s\n", lnk);

	if (readlink(lnk, file, sizeof(file)) < 0) {
		perror("readlink");
		return NULL;
	}
	printf("%s\n", basename(file));

	return basename(file);
}


int
main(int argc, char **argv)
{
	int opt, ccsfd;
	int severity = -1;

	char *logmsg = argv[argc-1];
	--argc;

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

	init_logging(log_name(), 1, severity);
	ccsfd = ccs_connect();
	setup_logging(ccsfd);
	ccs_disconnect(ccsfd);

	log_printf(severity, "%s\n", logmsg);

	close_logging();
	return 0;
}
