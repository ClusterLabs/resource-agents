/**
  @file Quorum disk utility
 */
#include <stdio.h>
#include <stdlib.h>
#include <disk.h>
#include <errno.h>
#include <sys/types.h>
#include <platform.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <corosync/engine/logsys.h>

int
main(int argc, char **argv)
{
	char device[128];
	char *newdev = NULL, *newlabel = NULL;
	int rv, verbose_level = 1;

	logsys_init("QDISK", LOG_MODE_OUTPUT_STDERR | LOG_MODE_NOSUBSYS, SYSLOGFACILITY, SYSLOGLEVEL, NULL);

	printf("mkqdisk v" RELEASE_VERSION "\n\n");

	/* XXX this is horrible but we need to prioritize options as long as
	 * we can't queue messages properly
	 */
	while ((rv = getopt(argc, argv, "Ldf:c:l:h")) != EOF) {
		switch (rv) {
		case 'd':
			++verbose_level;
			/* Workaround a bug in logsys new API.
			 * logsys segfaults if our first operation is to set the priority
			 * because the logsys_config_priority_set is buggy.
			 * Temporary use the direct call while fix is applied upstream and propagated
			 */
			// logsys_config_priority_set(LOG_LEVEL_DEBUG);
			_logsys_config_priority_set(0, LOG_LEVEL_DEBUG);
			break;
		}
	}

	/* reset the option index to reparse */
	optind = 0;

	while ((rv = getopt(argc, argv, "Ldf:c:l:h")) != EOF) {
		switch (rv) {
		case 'd':
			/* processed above, needs to be here for compat */
			break;
		case 'L':
			/* List */
			return find_partitions(NULL, NULL, 0, verbose_level);
		case 'f':
			return find_partitions( optarg, device,
					       sizeof(device), verbose_level);
		case 'c':
			newdev = optarg;
			break;
		case 'l':
			newlabel = optarg;
			break;
		case 'h':
			printf("usage: mkqdisk -L | -f <label> | -c "
			       "<device> -l <label>\n");
			return 0;
		default:
			break;
		}
	}

	if (!newdev && !newlabel) {
		printf("usage: mkqdisk -L | -f <label> | -c "
		       "<device> -l <label>\n");
		return 1;
	}

	if (!newdev || !newlabel) {
		printf("Both a device and a label are required\n");
		return 1;
	}

	printf("Writing new quorum disk label '%s' to %s.\n",
	       newlabel, newdev);
	printf("WARNING: About to destroy all data on %s; proceed [N/y] ? ",
	       newdev);
	if (getc(stdin) != 'y') {
		printf("Good thinking.\n");
		return 0;
	}

	return qdisk_init(newdev, newlabel);
}
