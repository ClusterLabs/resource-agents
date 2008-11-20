#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdint.h>
#include <string.h>
#include <liblogthread.h>

#include "libfence.h"
#include "libfenced.h"
#include "copyright.cf"

#define OPTION_STRING           ("huV")

#define die(fmt, args...) \
do \
{ \
  fprintf(stderr, "%s: ", prog_name); \
  fprintf(stderr, fmt "\n", ##args); \
  exit(EXIT_FAILURE); \
} \
while (0)

static char *prog_name;

static void print_usage(void)
{
	printf("Usage:\n");
	printf("\n");
	printf("%s [options] node_name\n", prog_name);
	printf("\n");
	printf("Options:\n");
	printf("\n");
	printf("  -h               Print this help, then exit\n");
	printf("  -V               Print program version information, then exit\n");
	printf("\n");
}

int main(int argc, char *argv[])
{
	int cont = 1, optchar, error, rv;
	char *victim = NULL;

	prog_name = argv[0];

	while (cont) {
		optchar = getopt(argc, argv, OPTION_STRING);

		switch (optchar) {

		case 'h':
			print_usage();
			exit(EXIT_SUCCESS);
			break;

		case 'V':
			printf("%s %s (built %s %s)\n", prog_name,
				RELEASE_VERSION, __DATE__, __TIME__);
			printf("%s\n", REDHAT_COPYRIGHT);
			exit(EXIT_SUCCESS);
			break;

		case ':':
		case '?':
			fprintf(stderr, "Please use '-h' for usage.\n");
			exit(EXIT_FAILURE);
			break;

		case EOF:
			cont = 0;
			break;

		default:
			die("unknown option: %c", optchar);
			break;
		};
	}

	while (optind < argc) {
		if (victim)
			die("unknown option %s", argv[optind]);
		victim = argv[optind];
		optind++;
	}

	if (!victim)
		die("no node name specified");

	error = fence_node(victim);

	logt_init("fence_node", LOG_MODE_OUTPUT_SYSLOG, SYSLOGFACILITY,
		  SYSLOGLEVEL, 0, NULL);

	if (error) {
		fprintf(stderr, "Fence of \"%s\" was unsuccessful\n", victim);
		logt_print(LOG_ERR, "Fence of \"%s\" was unsuccessful\n",
			   victim);
		rv = EXIT_FAILURE;
	} else {
		fprintf(stderr, "Fence of \"%s\" was successful\n", victim);
		logt_print(LOG_NOTICE, "Fence of \"%s\" was successful\n",
			   victim);
		rv = EXIT_SUCCESS;

		/* Tell fenced what we've done so that it can avoid fencing
		   this node again if the fence_node() rebooted it. */
		fenced_external(victim);
	}

	logt_exit();
	exit(rv);
}

