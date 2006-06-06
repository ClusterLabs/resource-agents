/*****************************************************************************
******************************************************************************
**
**  Copyright (C) 2005 Red Hat, Inc.  All rights reserved.
**
**  This copyrighted material is made available to anyone wishing to use,
**  modify, copy, or redistribute it subject to the terms and conditions
**  of the GNU General Public License v.2.
**
******************************************************************************
*****************************************************************************/

#include <unistd.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <libgen.h>
#include <string.h>
#include <stdarg.h>

#include "copyright.cf"
#include "libgfs2.h"
#include "fsck.h"
#include "log.h"
#include "osi_list.h"

struct options opts = {0};
struct gfs2_inode *lf_dip; /* Lost and found directory inode */
osi_list_t dir_hash[FSCK_HASH_SIZE];
osi_list_t inode_hash[FSCK_HASH_SIZE];
struct gfs2_block_list *bl;
uint64_t last_fs_block;
uint64_t last_data_block;
uint64_t first_data_block;
osi_list_t dup_list;
char *prog_name = "gfs2_fsck"; /* needed by libgfs2 */

/* This function is for libgfs2's sake.                                      */
void print_it(const char *label, const char *fmt, const char *fmt2, ...)
{
	va_list args;

	va_start(args, fmt2);
	printf("%s: ", label);
	vprintf(fmt, args);
	va_end(args);
}

void print_map(struct gfs2_block_list *il, int count)
{
	int i, j;
	struct gfs2_block_query k;

	log_info("Printing map of blocks - 80 blocks per row\n");
	j = 0;
	for(i = 0; i < count; i++) {
		if(j > 79) {
			log_info("\n");
			j = 0;
		}
		else if(!(j %10) && j != 0) {
			log_info(" ");
		}
		j++;
		gfs2_block_check(il, i, &k);
		log_info("%X", k.block_type);

	}
	log_info("\n");
}

void usage(char *name)
{
	printf("Usage: %s [-hnqvVy] <device> \n", basename(name));
}

void version(void)
{
	printf("GFS2 fsck %s (built %s %s)\n",
	       GFS_RELEASE_NAME, __DATE__, __TIME__);
	printf("%s\n", REDHAT_COPYRIGHT);
}

int read_cmdline(int argc, char **argv, struct options *opts)
{
	char c;

	while((c = getopt(argc, argv, "hnqvyV")) != -1) {
		switch(c) {

		case 'h':
			usage(argv[0]);
			exit(0);
			break;
		case 'n':
			opts->no = 1;
			break;
		case 'q':
			decrease_verbosity();
			break;
		case 'v':
			increase_verbosity();
			break;
		case 'V':
			version();
			exit(0);
			break;
		case 'y':
			opts->yes = 1;
			break;
		case ':':
		case '?':
			fprintf(stderr, "Please use '-h' for usage.\n");
			exit(1);
			break;
		default:
			fprintf(stderr, "Bad programmer! You forgot to catch"
				" the %c flag\n", c);
			exit(1);
			break;

		}
	}
	if(argc > optind) {
		opts->device = (argv[optind]);
		if(!opts->device) {
			fprintf(stderr, "Please use '-h' for usage.\n");
			exit(1);
		}
	} else {
		fprintf(stderr, "No device specified.  Use '-h' for usage.\n");
		exit(1);
	}
	return 0;
}

int main(int argc, char **argv)
{
	struct gfs2_sbd sb;
	struct gfs2_sbd *sbp = &sb;
	int j;

	memset(sbp, 0, sizeof(*sbp));

	if(read_cmdline(argc, argv, &opts))
		return 1;
	setbuf(stdout, NULL);
	log_notice("Initializing fsck\n");
	if (initialize(sbp))
		return 1;

	log_notice("Starting pass1\n");
	if (pass1(sbp))
		return 1;
	log_notice("Pass1 complete      \n");

	log_notice("Starting pass1b\n");
	if(pass1b(sbp))
		return 1;
	log_notice("Pass1b complete\n");

	log_notice("Starting pass1c\n");
	if(pass1c(sbp))
		return 1;
	log_notice("Pass1c complete\n");

	log_notice("Starting pass2\n");
	if (pass2(sbp))
		return 1;
	log_notice("Pass2 complete      \n");

	log_notice("Starting pass3\n");
	if (pass3(sbp))
		return 1;
	log_notice("Pass3 complete      \n");

	log_notice("Starting pass4\n");
	if (pass4(sbp))
		return 1;
	log_notice("Pass4 complete      \n");

	log_notice("Starting pass5\n");
	if (pass5(sbp))
		return 1;
	log_notice("Pass5 complete      \n");

	/* Free up our system inodes */
	inode_put(sbp->md.inum, updated);
	inode_put(sbp->md.statfs, updated);
	for (j = 0; j < sbp->md.journals; j++)
		inode_put(sbp->md.journal[j], updated);
	inode_put(sbp->md.jiinode, updated);
	inode_put(sbp->md.riinode, updated);
	inode_put(sbp->md.qinode, updated);
	inode_put(sbp->md.pinode, updated);
	inode_put(sbp->md.rooti, updated);
	inode_put(sbp->master_dir, updated);
	if (lf_dip)
		inode_put(lf_dip, updated);
/*	print_map(sbp->bl, sbp->last_fs_block); */

	bsync(sbp);
	destroy(sbp);
	log_notice("gfs2_fsck complete    \n");

	return 0;
}
