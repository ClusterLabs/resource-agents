#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <stdint.h>
#include <inttypes.h>

#include "gfs_ondisk.h"
#include "copyright.cf"

#define EXTERN
#include "gfs_debug.h"
#include "basic.h"
#include "block_device.h"
#include "readfile.h"

/**
 * print_usage - print out usage information
 *
 */

static void
print_usage(void)
{
	printf("Usage:\n");
	printf("\n");
	printf("%s [options] <action>\n", prog_name);
	printf("\n");
	printf("Actions:\n");
	printf("  size             print the device size\n");
	printf("  hexread          print a block in hex\n");
	printf("  rawread          print a block raw\n");
	printf("\n");
	printf("GFS-specific Actions:\n");
	printf("  scan             scan the device looking for GFS blocks\n");
	printf("  identify         identify the contents of a block\n");
	printf("  sb               print superblock\n");
	printf("  jindex           print journal index\n");
	printf("  rindex           print resource index\n");
	printf("  quota            print quota file\n");
	printf("  root             print root directory\n");
	printf("  readfile         print the contents of a file\n");
	printf("  readdir          print the contents of a directory\n");
	printf("\n");
	printf("Options:\n");
	printf("\n");
	printf("  -B <bytes>       Set the block size\n");
	printf("  -b <number>      Block number\n");
	printf("  -d <device>      Device to look at\n");
	printf("  -h               Print this help, then exit\n");
	printf("  -v               Verbose\n");
	printf("  -V               Print program version information, then exit\n");
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
		optchar = getopt(argc, argv, "B:b:d:hVv");

		switch (optchar) {
		case 'B':
			sscanf(optarg, "%u", &block_size);
			if (!block_size)
				die("can't have a zero block size\n");
			break;

		case 'b':
			sscanf(optarg, "%"SCNu64, &block_number);
			break;

		case 'd':
			device = optarg;
			break;

		case 'h':
			print_usage();
			exit(EXIT_SUCCESS);

		case 'V':
			printf("gfs_mkfs %s (built %s %s)\n", RELEASE_VERSION, __DATE__, __TIME__);
			printf("%s\n", REDHAT_COPYRIGHT);
			exit(EXIT_SUCCESS);

		case 'v':
			verbose++;
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

	if (optind < argc) 
		die("Unrecognized option: %s\n", argv[optind]);

	if (!device)
		die("no device specified\n");
}

/**
 * main - 
 * @argc:
 * @argv:
 *
 * Returns: exit status
 */

int
main(int argc, char *argv[])
{
	prog_name = argv[0];

	decode_arguments(argc, argv);

	device_fd = open(device, O_RDWR);
	if (device_fd < 0)
		die("can't open device %s: %s\n",
		    device, strerror(errno));

	find_device_size();
	verify_gfs();

	if (!strcmp(action, "size"))
		print_size();
	else if (!strcmp(action, "hexread"))
		print_hexblock();
	else if (!strcmp(action, "rawread"))
		print_rawblock();
	else if (!strcmp(action, "scan"))
		scan_device();
	else if (!strcmp(action, "identify"))
		identify_block();
	else if (!strcmp(action, "sb"))
		print_superblock();
	else if (!strcmp(action, "jindex"))
		print_jindex();
	else if (!strcmp(action, "rindex"))
		print_rindex();
	else if (!strcmp(action, "quota"))
		print_quota();
	else if (!strcmp(action, "root"))
		print_root();
	else if (!strcmp(action, "readfile"))
		readfile();
	else if (!strcmp(action, "readdir"))
		readdir();
	else
		die("unknown action %s\n", action);
		
	close(device_fd);

	exit(EXIT_SUCCESS);
}

