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
#include <libgen.h>

#include "copyright.cf"
#include "fsck_incore.h"
#include "fsck.h"
#include "log.h"

uint64_t last_fs_block;

void print_map(struct block_list *il, int count)
{
	int i, j;
	struct block_query k;

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
		block_check(il, i, &k);
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
	printf("GFS fsck %s (built %s %s)\n",
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
	struct fsck_sb sb;
	struct options opts = {0};

	struct fsck_sb *sbp = &sb;
	memset(sbp, 0, sizeof(*sbp));

	sbp->opts = &opts;

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
	log_notice("Pass1b complete      \n");

	log_notice("Starting pass1c\n");
	if(pass1c(sbp))
		return 1;
	log_notice("Pass1c complete      \n");

	log_notice("Starting pass2\n");
	if (pass2(sbp, &opts))
		return 1;
	log_notice("Pass2 complete      \n");

	log_notice("Starting pass3\n");
	if (pass3(sbp, &opts))
		return 1;
	log_notice("Pass3 complete      \n");

	log_notice("Starting pass4\n");
	if (pass4(sbp, &opts))
		return 1;
	log_notice("Pass4 complete      \n");

	log_notice("Starting pass5\n");
	if (pass5(sbp, &opts))
		return 1;
	log_notice("Pass5 complete      \n");

/*	print_map(sbp->bl, sbp->last_fs_block); */

	log_notice("Writing changes to disk\n");
	destroy(sbp);

	return 0;
}



