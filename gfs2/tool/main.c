#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <inttypes.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <limits.h>
#include <errno.h>
#include <linux/types.h>

#include "copyright.cf"

#include "gfs2_tool.h"
#include "libgfs2.h"

char *prog_name;
char *action = NULL;
int override = FALSE;
int expert = FALSE;
int debug = FALSE;
int continuous = FALSE;
int interval = 1;

static const char *usage[] = {
	"Clear a flag on a inode\n",
	"  gfs2_tool clearflag flag <filenames>\n",
	"\n",
	"Do a GFS2 specific \"df\":\n",
	"  gfs2_tool df <mountpoint>\n",
	"\n",
	"Freeze a GFS2 cluster:\n",
	"  gfs2_tool freeze <mountpoint>\n",
	"\n",
	"Print the current mount arguments of a mounted filesystem:\n",
	"  gfs2_tool getargs <mountpoint>\n",
	"\n",
	"Get tuneable parameters for a filesystem\n",
	"  gfs2_tool gettune <mountpoint>\n",
	"\n",
	"List the file system's journals:\n",
	"  gfs2_tool journals <mountpoint>\n",
	"\n",
	"List filesystems:\n",
	"  gfs2_tool list\n",
	"\n",
	"Have GFS2 dump its lock state:\n",
	"  gfs2_tool lockdump <mountpoint> [buffersize]\n",
	"\n",
	"Provide arguments for next mount:\n",
	"  gfs2_tool margs <mountarguments>\n",
	"\n",
	"Tune a GFS2 superblock\n",
	"  gfs2_tool sb <device> proto [newval]\n",
	"  gfs2_tool sb <device> table [newval]\n",
	"  gfs2_tool sb <device> ondisk [newval]\n",
	"  gfs2_tool sb <device> multihost [newval]\n",
	"  gfs2_tool sb <device> all\n",
	"\n",
	"Set a flag on a inode\n",
	"  gfs2_tool setflag flag <filenames>\n",
	"\n",
	"Tune a running filesystem\n",
	"  gfs2_tool settune <mountpoint> <parameter> <value>\n",
	"\n",
	"Shrink a filesystem's inode cache:\n",
	"  gfs2_tool shrink <mountpoint>\n",
	"\n",
	"Unfreeze a GFS2 cluster:\n",
	"  gfs2_tool unfreeze <mountpoint>\n",
	"\n",
	"Print tool version information\n",
	"  gfs2_tool version\n",
	"\n",
	"Withdraw this machine from participating in a filesystem:\n",
	"  gfs2_tool withdraw <mountpoint>\n",
#if GFS2_TOOL_FEATURE_IMPLEMENTED
	"\n",
	"Force files from a machine's cache\n",
	"  gfs2_tool flush <filenames>\n",
	"\n",
	"Print the superblock of a mounted filesystem:\n",
	"  gfs2_tool getsb <mountpoint>\n",
	"\n",
	"Print the journal index of a mounted filesystem:\n",
	"  gfs2_tool jindex <mountpoint>\n",
	"\n",
	"Print out the ondisk layout for a file:\n",
	"  gfs2_tool layout <filename> [buffersize]\n",
	"\n",
	"Print the quota file of a mounted filesystem:\n",
	"  gfs2_tool quota <mountpoint>\n",
	"\n",
	"Print the resource group index of a mounted filesystem:\n",
	"  gfs2_tool rindex <mountpoint>\n",
	"\n",
	"Print file stat data:\n",
	"  gfs2_tool stat <filename>\n",
	"\n",
#endif /* GFS2_TOOL_FEATURE_IMPLEMENTED */
	"",
};

/**
 * print_usage - print out usage information
 *
 */

void
print_usage(void)
{
	int x;

	for (x = 0; usage[x][0]; x++)
		printf("%s", usage[x]);
}

/**
 * print_version -
 *
 */

static void
print_version(void)
{
	printf("gfs2_tool %s (built %s %s)\n",
	       RELEASE_VERSION,
	       __DATE__, __TIME__);
	printf("%s\n",
	       REDHAT_COPYRIGHT);
}

/**
 * decode_arguments -
 * @argc:
 * @argv:
 *
 */

static void
decode_arguments(int argc, char *argv[])
{
	int cont = TRUE;
	int optchar;

	while (cont) {
		optchar = getopt(argc, argv, "cDhi:OVX");

		switch (optchar) {
		case 'c':
			continuous = TRUE;
			break;

		case 'D':
			debug = TRUE;
			break;

		case 'h':
			print_usage();
			exit(EXIT_SUCCESS);

		case 'i':
			sscanf(optarg, "%u", &interval);
			break;

		case 'O':
			override = TRUE;
			break;

		case 'V':
			print_version();
			exit(EXIT_SUCCESS);

		case 'X':
			expert = TRUE;
			break;

		case EOF:
			cont = FALSE;
			break;

		default:
			die("unknown option: %c\n", optchar);
		};
	}

	if (optind < argc) {
		action = argv[optind];
		optind++;
	} else
		die("no action specified\n");
}

/**
 * main - Do everything
 * @argc:
 * @argv:
 *
 */

int
main(int argc, char *argv[])
{
	prog_name = argv[0];

	if (argc < 2) {
		print_usage();
		exit(EXIT_SUCCESS);
	}

	decode_arguments(argc, argv);

	if (strcmp(action, "clearflag") == 0)
		set_flag(argc, argv);
	else if (strcmp(action, "df") == 0)
		print_df(argc, argv);
	else if (strcmp(action, "freeze") == 0)
		do_freeze(argc, argv);
	else if (strcmp(action, "getargs") == 0)
		print_args(argc, argv);
	else if (strcmp(action, "gettune") == 0)
		get_tune(argc, argv);
	else if (strcmp(action, "journals") == 0)
		print_journals(argc, argv);
	else if (strcmp(action, "list") == 0)
		print_list();
	else if (strcmp(action, "lockdump") == 0)
		print_lockdump(argc, argv);
	else if (strcmp(action, "margs") == 0)
		margs(argc, argv);
	else if (strcmp(action, "sb") == 0)
		do_sb(argc, argv);
	else if (strcmp(action, "setflag") == 0)
		set_flag(argc, argv);
	else if (strcmp(action, "settune") == 0)
		set_tune(argc, argv);
	else if (strcmp(action, "shrink") == 0)
		do_shrink(argc, argv);
	else if (strcmp(action, "unfreeze") == 0)
		do_freeze(argc, argv);
	else if (strcmp(action, "version") == 0)
		print_version();
	else if (strcmp(action, "withdraw") == 0)
		do_withdraw(argc, argv);
#if GFS2_TOOL_FEATURE_IMPLEMENTED
	else if (strcmp(action, "flush") == 0)
		do_file_flush(argc, argv);
	else if (strcmp(action, "getsb") == 0)
		print_sb(argc, argv);
	else if (strcmp(action, "jindex") == 0)
		print_jindex(argc, argv);
	else if (strcmp(action, "layout") == 0)
		print_layout(argc, argv);
	else if (strcmp(action, "quota") == 0)
		print_quota(argc, argv);
	else if (strcmp(action, "rindex") == 0)
		print_rindex(argc, argv);
	else if (strcmp(action, "stat") == 0)
		print_stat(argc, argv);
#endif /* #if GFS2_TOOL_FEATURE_IMPLEMENTED */
	else
		die("unknown action: %s\n",
		    action);

	exit(EXIT_SUCCESS);
}
