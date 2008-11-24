#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <inttypes.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>
#include <errno.h>
#include <stdarg.h>
#include <mntent.h>
#include <ctype.h>

#include <linux/types.h>
#include "libgfs2.h"
#include "gfs2_mkfs.h"
#include "libvolume_id.h"

char *prog_name;

/**
 * This function is for libgfs2's sake.
 */
void print_it(const char *label, const char *fmt, const char *fmt2, ...)
{
	va_list args;

	va_start(args, fmt2);
	printf("%s: ", label);
	vprintf(fmt, args);
	va_end(args);
}

/**
 * print_usage - print out usage information
 *
 */

static void
print_usage(void)
{
	printf("Usage:\n");
	printf("\n");
	printf("%s [options] <device> [ block-count ]\n", prog_name);
	printf("\n");
	printf("Options:\n");
	printf("\n");
	printf("  -b <bytes>       Filesystem block size\n");
	printf("  -c <MB>          Size of quota change file\n");
	printf("  -D               Enable debugging code\n");
	printf("  -h               Print this help, then exit\n");
	printf("  -J <MB>          Size of journals\n");
	printf("  -j <num>         Number of journals\n");
	printf("  -O               Don't ask for confirmation\n");
	printf("  -p <name>        Name of the locking protocol\n");
	printf("  -q               Don't print anything\n");
	printf("  -r <MB>          Resource Group Size\n");
	printf("  -t <name>        Name of the lock table\n");
	printf("  -u <MB>          Size of unlinked file\n");
	printf("  -V               Print program version information, then exit\n");
}

/**
 * decode_arguments - decode command line arguments and fill in the struct gfs2_sbd
 * @argc:
 * @argv:
 * @sdp: the decoded command line arguments
 *
 */

static void
decode_arguments(int argc, char *argv[], struct gfs2_sbd *sdp)
{
	int cont = TRUE;
	int optchar;

	memset(sdp->device_name, 0, sizeof(sdp->device_name));
	sdp->md.journals = 1;
	sdp->orig_fssize = 0;

	while (cont) {
		optchar = getopt(argc, argv, "-b:c:DhJ:j:Op:qr:t:u:VX");

		switch (optchar) {
		case 'b':
			sdp->bsize = atoi(optarg);
			break;

		case 'c':
			sdp->qcsize = atoi(optarg);
			break;

		case 'D':
			sdp->debug = TRUE;
			break;

		case 'h':
			print_usage();
			exit(EXIT_SUCCESS);
			break;

		case 'J':
			sdp->jsize = atoi(optarg);
			break;

		case 'j':
			sdp->md.journals = atoi(optarg);
			break;

		case 'O':
			sdp->override = TRUE;
			break;

		case 'p':
			if (strlen(optarg) >= GFS2_LOCKNAME_LEN)
				die("lock protocol name %s is too long\n",
				    optarg);
			strcpy(sdp->lockproto, optarg);
			break;

		case 'q':
			sdp->quiet = TRUE;
			break;

		case 'r':
			sdp->rgsize = atoi(optarg);
			break;

		case 't':
			if (strlen(optarg) >= GFS2_LOCKNAME_LEN)
				die("lock table name %s is too long\n", optarg);
			strcpy(sdp->locktable, optarg);
			break;

		case 'u':
			sdp->utsize = atoi(optarg);
			break;

		case 'V':
			printf("gfs2_mkfs %s (built %s %s)\n", RELEASE_VERSION,
			       __DATE__, __TIME__);
			printf("%s\n", REDHAT_COPYRIGHT);
			exit(EXIT_SUCCESS);
			break;

		case 'X':
			sdp->expert = TRUE;
			break;

		case ':':
		case '?':
			fprintf(stderr, "Please use '-h' for usage.\n");
			exit(EXIT_FAILURE);
			break;

		case EOF:
			cont = FALSE;
			break;

		case 1:
			if (strcmp(optarg, "gfs2") == 0)
				continue;
			if (!sdp->device_name[0])
				strcpy(sdp->device_name, optarg);
			else if (!sdp->orig_fssize &&
				 isdigit(optarg[0]))
				sdp->orig_fssize = atol(optarg);
			else
				die("More than one device specified (try -h for help)\n");
			break;

		default:
			die("unknown option: %c\n", optchar);
			break;
		};
	}

	if ((sdp->device_name[0] == 0) && (optind < argc))
		strcpy(sdp->device_name, argv[optind++]);

	if (sdp->device_name[0] == '\0')
		die("no device specified (try -h for help)\n");

	if (optind < argc)
		sdp->orig_fssize = atol(argv[optind++]);

	if (optind < argc)
		die("Unrecognized argument: %s\n", argv[optind]);

	if (sdp->debug) {
		printf("Command Line Arguments:\n");
		printf("  bsize = %u\n", sdp->bsize);
		printf("  qcsize = %u\n", sdp->qcsize);
		printf("  jsize = %u\n", sdp->jsize);
		printf("  journals = %u\n", sdp->md.journals);
		printf("  override = %d\n", sdp->override);
		printf("  proto = %s\n", sdp->lockproto);
		printf("  quiet = %d\n", sdp->quiet);
		if (sdp->rgsize==-1)
			printf("  rgsize = optimize for best performance\n");
		else
			printf("  rgsize = %u\n", sdp->rgsize);
		printf("  table = %s\n", sdp->locktable);
		printf("  utsize = %u\n", sdp->utsize);
		printf("  device = %s\n", sdp->device_name);
		if (sdp->orig_fssize)
			printf("  block-count = %llu\n",
			       (unsigned long long)sdp->orig_fssize);
	}
}

static void
verify_arguments(struct gfs2_sbd *sdp)
{
	unsigned int x;

	if (!sdp->expert)
		test_locking(sdp->lockproto, sdp->locktable);

	/* Block sizes must be a power of two from 512 to 65536 */

	for (x = 512; x; x <<= 1)
		if (x == sdp->bsize)
			break;

	if (!x || sdp->bsize > 65536)
		die("block size must be a power of two between 512 and 65536\n");

	/* Look at this!  Why can't we go bigger than 2GB? */
	if (sdp->expert) {
		if (1 > sdp->rgsize || sdp->rgsize > 2048)
			die("bad resource group size\n");
	} else {
		if (32 > sdp->rgsize || sdp->rgsize > 2048)
			die("bad resource group size\n");
	}

	if (!sdp->md.journals)
		die("no journals specified\n");

	if (sdp->jsize < 8 || sdp->jsize > 1024)
		die("bad journal size\n");

	if (!sdp->utsize || sdp->utsize > 64)
		die("bad unlinked size\n");

	if (!sdp->qcsize || sdp->qcsize > 64)
		die("bad quota change size\n");
}

/**
 * are_you_sure - protect lusers from themselves
 * @sdp: the command line
 *
 */

static void are_you_sure(struct gfs2_sbd *sdp)
{
	char input[32];
	struct volume_id *vid = NULL;
	int fd;

	fd = open(sdp->device_name, O_RDONLY);
	if (fd < 0)
		die("Error: device %s not found.\n", sdp->device_name);
	vid = volume_id_open_fd(fd);
	if (vid == NULL) {
		close(fd);
		die("error identifying the contents of %s: %s\n",
		    sdp->device_name, strerror(errno));
	}
	printf("This will destroy any data on %s.\n", sdp->device_name);
	if (volume_id_probe_all(vid, 0, sdp->device_size) == 0) {
		const char *fstype, *fsusage;
		int rc;

		rc = volume_id_get_type(vid, &fstype);
		if (rc) {
			rc = volume_id_get_usage(vid, &fsusage);
			if (!rc || strncmp(fsusage, "other", 5) == 0)
				fsusage = "partition";
			printf("  It appears to contain a %s %s.\n", fstype,
			       fsusage);
		}
	}
	volume_id_close(vid);
	close(fd);
	printf("\nAre you sure you want to proceed? [y/n] ");
	if(!fgets(input, 32, stdin))
		die("unable to read from stdin\n");

	if (input[0] != 'y')
		die("aborted\n");
	else
		printf("\n");
}

/**
 * check_mount - check to see if device is mounted/busy
 * @device: the device to create the filesystem on
 *
 */

void check_mount(char *device)
{
	struct stat st_buf;
	int fd;

	if (stat(device, &st_buf) < 0)
		die("could not stat device %s\n", device);
	if (!S_ISBLK(st_buf.st_mode))
		die("%s is not a block device\n", device);

	fd = open(device, O_RDONLY | O_NONBLOCK | O_EXCL);

	if (fd < 0) {
		if (errno == EBUSY) {
			die("device %s is busy\n", device);
		}
	}
	else {
		close(fd);
	}

	return;
}

/**
 * print_results - print out summary information
 * @sdp: the command line
 *
 */

static void
print_results(struct gfs2_sbd *sdp, uint64_t real_device_size)
{
	if (sdp->debug)
		printf("\n");
	else if (sdp->quiet)
		return;

	if (sdp->expert)
		printf("Expert mode:               on\n");

	printf("Device:                    %s\n", sdp->device_name);

	printf("Blocksize:                 %u\n", sdp->bsize);
	printf("Device Size                %.2f GB (%"PRIu64" blocks)\n",
	       real_device_size / ((float)(1 << 30)),
	       real_device_size / sdp->bsize);
	printf("Filesystem Size:           %.2f GB (%"PRIu64" blocks)\n",
	       sdp->fssize / ((float)(1 << 30)) * sdp->bsize, sdp->fssize);

	printf("Journals:                  %u\n", sdp->md.journals);
	printf("Resource Groups:           %"PRIu64"\n", sdp->rgrps);

	printf("Locking Protocol:          \"%s\"\n", sdp->lockproto);
	printf("Lock Table:                \"%s\"\n", sdp->locktable);

	if (sdp->debug) {
		printf("\n");
		printf("Spills:                    %u\n", sdp->spills);
		printf("Writes:                    %u\n", sdp->writes);
	}

	printf("\n");
}

/**
 * main_mkfs - do everything
 * @argc:
 * @argv:
 *
 */

void
main_mkfs(int argc, char *argv[])
{
	struct gfs2_sbd sbd, *sdp = &sbd;
	unsigned int x;
	int error;
	int rgsize_specified = 0;
	uint64_t real_device_size;

	memset(sdp, 0, sizeof(struct gfs2_sbd));
	sdp->bsize = GFS2_DEFAULT_BSIZE;
	sdp->jsize = GFS2_DEFAULT_JSIZE;
	sdp->rgsize = -1;
	sdp->utsize = GFS2_DEFAULT_UTSIZE;
	sdp->qcsize = GFS2_DEFAULT_QCSIZE;
	strcpy(sdp->lockproto, GFS2_DEFAULT_LOCKPROTO);
	sdp->time = time(NULL);
	osi_list_init(&sdp->rglist);
	osi_list_init(&sdp->buf_list);
	for (x = 0; x < BUF_HASH_SIZE; x++)
		osi_list_init(&sdp->buf_hash[x]);

	decode_arguments(argc, argv, sdp);
	if (sdp->rgsize == -1)                 /* if rg size not specified */
		sdp->rgsize = GFS2_DEFAULT_RGSIZE; /* default it for now */
	else
		rgsize_specified = TRUE;

	verify_arguments(sdp);

	check_mount(sdp->device_name);

	sdp->device_fd = open(sdp->device_name, O_RDWR);
	if (sdp->device_fd < 0)
		die("can't open device %s: %s\n",
		    sdp->device_name, strerror(errno));

	if (!sdp->override)
		are_you_sure(sdp);

	compute_constants(sdp);

	/* Get the device geometry */

	device_size(sdp->device_fd, &real_device_size);
	device_geometry(sdp);
	/* Convert optional block-count to basic blocks */
	if (sdp->orig_fssize) {
		sdp->orig_fssize *= sdp->bsize;
		sdp->orig_fssize >>= GFS2_BASIC_BLOCK_SHIFT;
		if (sdp->orig_fssize > sdp->device.length) {
			fprintf(stderr, "%s: Specified block count is bigger "
				"than the actual device.\n", prog_name);
			die("Device Size is %.2f GB (%"PRIu64" blocks)\n",
			       real_device_size / ((float)(1 << 30)),
			       real_device_size / sdp->bsize);
		}
		sdp->device.length = sdp->orig_fssize;
	}
	fix_device_geometry(sdp);

	/* Compute the resource group layouts */

	compute_rgrp_layout(sdp, rgsize_specified);

	/* Build ondisk structures */

	build_rgrps(sdp, TRUE);
	build_root(sdp);
	build_master(sdp);
	build_sb(sdp);
	build_jindex(sdp);
	build_per_node(sdp);
	build_inum(sdp);
	build_statfs(sdp);
	build_rindex(sdp);
	build_quota(sdp);

	do_init(sdp);

	/* Cleanup */

	inode_put(sdp->md.rooti, updated);
	inode_put(sdp->master_dir, updated);
	inode_put(sdp->md.inum, updated);
	inode_put(sdp->md.statfs, updated);
	bsync(sdp);

	error = fsync(sdp->device_fd);
	if (error)
		die("can't fsync device (%d): %s\n",
		    error, strerror(errno));
	error = close(sdp->device_fd);
	if (error)
		die("error closing device (%d): %s\n",
		    error, strerror(errno));

	print_results(sdp, real_device_size);
}
