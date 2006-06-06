/******************************************************************************
*******************************************************************************
**
**  Copyright (C) Sistina Software, Inc.  1997-2003  All rights reserved.
**  Copyright (C) 2004 Red Hat, Inc.  All rights reserved.
**
**  This copyrighted material is made available to anyone wishing to use,
**  modify, copy, or redistribute it subject to the terms and conditions
**  of the GNU General Public License v.2.
**
*******************************************************************************
******************************************************************************/

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

#include <linux/types.h>
#include "gfs2_mkfs.h"
#include "libgfs2.h"
#ifdef VOLUME_ID
#include "libvolume_id.h"
#else
/* ========================================================================= */
/*                                                                           */
/*                                                                           */
/*                                                                           */
/*                                                                           */
/*                                                                           */
/*                                                                           */
/* NOTICE: Rip all this out once udev's libvolume_id is shipped as a         */
/*         standard library we can link against.                             */
/*                                                                           */
/*                                                                           */
/*                                                                           */
/*                                                                           */
/*                                                                           */
/*                                                                           */
/* ========================================================================= */
#define VOLUME_ID_LABEL_SIZE            64
#define VOLUME_ID_UUID_SIZE             36
#define VOLUME_ID_FORMAT_SIZE           32
#define VOLUME_ID_PATH_MAX              256
#define VOLUME_ID_PARTITIONS_MAX        256
#define SB_BUFFER_SIZE                           0x11000
#define SEEK_BUFFER_SIZE                 0x10000

#define GFS_MAGIC               0x01161970
#define GFS_DEFAULT_BSIZE       4096
#define GFS_SUPERBLOCK_OFFSET   (0x10 * GFS_DEFAULT_BSIZE)
#define GFS_METATYPE_SB         1
#define GFS_FORMAT_SB           100
#define GFS_LOCKNAME_LEN        64

/* gfs1 constants: */
#define GFS_FORMAT_FS           1309
#define GFS_FORMAT_MULTI        1401

enum volume_id_usage {
        VOLUME_ID_UNUSED,
        VOLUME_ID_UNPROBED,
        VOLUME_ID_OTHER,
        VOLUME_ID_FILESYSTEM,
        VOLUME_ID_RAID,
        VOLUME_ID_DISKLABEL,
        VOLUME_ID_CRYPTO,
};

struct volume_id {
        uint8_t         label_raw[VOLUME_ID_LABEL_SIZE];
        size_t          label_raw_len;
        char            label[VOLUME_ID_LABEL_SIZE+1];
        uint8_t         uuid_raw[VOLUME_ID_UUID_SIZE];
        size_t          uuid_raw_len;
        char            uuid[VOLUME_ID_UUID_SIZE+1];
        enum            volume_id_usage usage_id;
        char            *usage;
        char            *type;
        char            type_version[VOLUME_ID_FORMAT_SIZE];

        int             fd;
        uint8_t         *sbbuf;
        size_t          sbbuf_len;
        uint8_t         *seekbuf;
        uint64_t        seekbuf_off;
        size_t          seekbuf_len;
        int             fd_close:1;
};

uint8_t *volume_id_get_buffer(struct volume_id *id, uint64_t off, size_t len)
{
	ssize_t buf_len;

	/* check if requested area fits in superblock buffer */
	if (off + len <= SB_BUFFER_SIZE) {
		if (id->sbbuf == NULL) {
			id->sbbuf = malloc(SB_BUFFER_SIZE);
			if (id->sbbuf == NULL) {
				return NULL;
			}
		}
		
		/* check if we need to read */
		if ((off + len) > id->sbbuf_len) {
			if (lseek(id->fd, 0, SEEK_SET) < 0) {
				return NULL;
			}
			buf_len = read(id->fd, id->sbbuf, off + len);
			if (buf_len < 0) {
				return NULL;
			}
			id->sbbuf_len = buf_len;
			if ((size_t)buf_len < off + len) {
				return NULL;
			}
		}
		
		return &(id->sbbuf[off]);
	} else {
		if (len > SEEK_BUFFER_SIZE) {
			return NULL;
		}
		/* get seek buffer */
		if (id->seekbuf == NULL) {
			id->seekbuf = malloc(SEEK_BUFFER_SIZE);
			if (id->seekbuf == NULL) {
				return NULL;
			}
		}
		
		/* check if we need to read */
		if ((off < id->seekbuf_off) || ((off + len) > (id->seekbuf_off + id->seekbuf_len))) {
			if (lseek(id->fd, off, SEEK_SET) < 0) {
				return NULL;
			}
			buf_len = read(id->fd, id->seekbuf, len);
			if (buf_len < 0) {
				return NULL;
			}
			id->seekbuf_off = off;
			id->seekbuf_len = buf_len;
			if ((size_t)buf_len < len) {
				return NULL;
			}
		}
		
		return &(id->seekbuf[off - id->seekbuf_off]);
	}
}

void volume_id_free_buffer(struct volume_id *id)
{
	if (id->sbbuf != NULL) {
		free(id->sbbuf);
		id->sbbuf = NULL;
		id->sbbuf_len = 0;
	}
	if (id->seekbuf != NULL) {
		free(id->seekbuf);
		id->seekbuf = NULL;
		id->seekbuf_len = 0;
	}
}

static char *usage_to_string(enum volume_id_usage usage_id)
{
        switch (usage_id) {
        case VOLUME_ID_FILESYSTEM:
                return "filesystem";
        case VOLUME_ID_OTHER:
                return "other";
        case VOLUME_ID_RAID:
                return "raid";
        case VOLUME_ID_DISKLABEL:
                return "disklabel";
        case VOLUME_ID_CRYPTO:
                return "crypto";
        case VOLUME_ID_UNPROBED:
                return "unprobed";
        case VOLUME_ID_UNUSED:
                return "unused";
        }
        return NULL;
}

void volume_id_set_usage(struct volume_id *id, enum volume_id_usage usage_id)
{
	id->usage_id = usage_id;
	id->usage = usage_to_string(usage_id);
}

int volume_id_probe_gfs_generic(struct volume_id *id, uint64_t off, int vers)
{
	struct gfs2_sb *sbd;

	sbd = (struct gfs2_sb *) volume_id_get_buffer(id,
												  off + GFS_SUPERBLOCK_OFFSET,
												  sizeof(struct gfs2_sb));
	if (sbd == NULL)
		return -1;

	if (be32_to_cpu(sbd->sb_header.mh_magic) == GFS_MAGIC &&
		be32_to_cpu(sbd->sb_header.mh_type) == GFS_METATYPE_SB &&
		be32_to_cpu(sbd->sb_header.mh_format) == GFS_FORMAT_SB) {
		if (vers == 1) {
			if (be32_to_cpu(sbd->sb_fs_format) != GFS_FORMAT_FS ||
				be32_to_cpu(sbd->sb_multihost_format) != GFS_FORMAT_MULTI)
				return -1; /* not gfs1 */
			id->type = "gfs";
		}
		else if (vers == 2) {
			if (be32_to_cpu(sbd->sb_fs_format) != GFS2_FORMAT_FS ||
				be32_to_cpu(sbd->sb_multihost_format) != GFS2_FORMAT_MULTI)
				return -1; /* not gfs2 */
			id->type = "gfs2";
		}
		else
			return -1;
		strcpy(id->type_version, "1");
		volume_id_set_usage(id, VOLUME_ID_FILESYSTEM);
		return 0;
	}
	return -1;
}

int volume_id_probe_gfs(struct volume_id *id, uint64_t off)
{
        return (volume_id_probe_gfs_generic(id, off, 1));
}

int volume_id_probe_gfs2(struct volume_id *id, uint64_t off)
{
        return (volume_id_probe_gfs_generic(id, off, 2));
}

/* open volume by already open file descriptor */
struct volume_id *volume_id_open_fd(int fd)
{
        struct volume_id *id;

        id = malloc(sizeof(struct volume_id));
        if (id == NULL)
                return NULL;
        memset(id, 0x00, sizeof(struct volume_id));

        id->fd = fd;

        return id;
}

struct volume_id *volume_id_open_node(const char *path)
{
        struct volume_id *id;
        int fd;

        fd = open(path, O_RDONLY);
        if (fd < 0) {
                return NULL;
        }
        id = volume_id_open_fd(fd);
        if (id == NULL)
                return NULL;

        /* close fd on device close */
        id->fd_close = 1;
        return id;
}

int volume_id_probe_all(struct volume_id *id, uint64_t off, uint64_t size)
{
	volume_id_get_buffer(id, 0, SB_BUFFER_SIZE);
	if (volume_id_probe_gfs2(id, off) == 0) {
        volume_id_free_buffer(id);
        return 0;
	}
	return -1;
}

void volume_id_close(struct volume_id *id)
{
	if (id == NULL)
		return;
	if (id->fd_close != 0)
		close(id->fd);
	volume_id_free_buffer(id);
	free(id);
}
#endif

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
	printf("%s [options] <device>\n", prog_name);
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

	sdp->device_name = NULL;

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
			printf("gfs2_mkfs %s (built %s %s)\n", GFS2_RELEASE_NAME,
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
			if (sdp->device_name) {
				die("More than one device specified (try -h for help)");
			} 
			sdp->device_name = optarg;
			break;

		default:
			die("unknown option: %c\n", optchar);
			break;
		};
	}

	if ((sdp->device_name == NULL) && (optind < argc)) {
		sdp->device_name = argv[optind++];
	}

	if (sdp->device_name == NULL)
		die("no device specified (try -h for help)\n");

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
		printf("  rgsize = %u\n", sdp->rgsize);
		printf("  table = %s\n", sdp->locktable);
		printf("  utsize = %u\n", sdp->utsize);
		printf("  device = %s\n", sdp->device_name);
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

	vid = volume_id_open_node(sdp->device_name);
	if (vid == NULL)
		die("error identifying the contents of %s: %s\n",
		    sdp->device_name, strerror(errno));

	printf("This will destroy any data on %s.\n",
	       sdp->device_name);
	if (volume_id_probe_all(vid, 0, sdp->device_size) == 0)
		printf("  It appears to contain a %s %s.\n", vid->type,
			   vid->usage_id == VOLUME_ID_OTHER? "partition" : vid->usage);
	volume_id_close(vid);
	printf("\nAre you sure you want to proceed? [y/n] ");
	fgets(input, 32, stdin);

	if (input[0] != 'y')
		die("aborted\n");
	else
		printf("\n");
}

/**
 * print_results - print out summary information
 * @sdp: the command line
 *
 */

static void
print_results(struct gfs2_sbd *sdp)
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
	       sdp->device_size / ((float)(1 << 30)) * sdp->bsize, sdp->device_size);
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

	memset(sdp, 0, sizeof(struct gfs2_sbd));
	sdp->bsize = GFS2_DEFAULT_BSIZE;
	sdp->jsize = GFS2_DEFAULT_JSIZE;
	sdp->rgsize = GFS2_DEFAULT_RGSIZE;
	sdp->utsize = GFS2_DEFAULT_UTSIZE;
	sdp->qcsize = GFS2_DEFAULT_QCSIZE;
	sdp->time = time(NULL);
	osi_list_init(&sdp->rglist);
	osi_list_init(&sdp->buf_list);
	for (x = 0; x < BUF_HASH_SIZE; x++)
		osi_list_init(&sdp->buf_hash[x]);

	decode_arguments(argc, argv, sdp);
	verify_arguments(sdp);

	sdp->device_fd = open(sdp->device_name, O_RDWR);
	if (sdp->device_fd < 0)
		die("can't open device %s: %s\n",
		    sdp->device_name, strerror(errno));

	if (!sdp->override)
		are_you_sure(sdp);

	compute_constants(sdp);

	/* Get the device geometry */

	device_geometry(sdp);
	fix_device_geometry(sdp);

	/* Compute the resource group layouts */

	compute_rgrp_layout(sdp, TRUE);

	/* Build ondisk structures */

	build_rgrps(sdp);
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

	print_results(sdp);
}
