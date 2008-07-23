#include <unistd.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <libgen.h>
#include <string.h>
#include <stdarg.h>
#include <ctype.h>
#include <signal.h>

#include "copyright.cf"
#include "libgfs2.h"
#include "fsck.h"
#include "osi_list.h"

struct gfs2_options opts = {0};
struct gfs2_inode *lf_dip; /* Lost and found directory inode */
osi_list_t dir_hash[FSCK_HASH_SIZE];
osi_list_t inode_hash[FSCK_HASH_SIZE];
struct gfs2_block_list *bl;
uint64_t last_fs_block, last_reported_block = -1;
int skip_this_pass = FALSE, fsck_abort = FALSE;
const char *pass = "";
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

void usage(char *name)
{
	printf("Usage: %s [-hnqvVy] <device> \n", basename(name));
}

void version(void)
{
	printf("GFS2 fsck %s (built %s %s)\n",
	       RELEASE_VERSION, __DATE__, __TIME__);
	printf("%s\n", REDHAT_COPYRIGHT);
}

int read_cmdline(int argc, char **argv, struct gfs2_options *opts)
{
	int c;

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

void interrupt(int sig)
{
	char response;
	char progress[PATH_MAX];

	if (!last_reported_block || last_reported_block == last_fs_block)
		sprintf(progress, "progress unknown.\n");
	else
		sprintf(progress, "processing block %" PRIu64 " out of %"
			PRIu64 "\n", last_reported_block, last_fs_block);
	
	response = generic_interrupt("gfs2_fsck", pass, progress,
				     "Do you want to abort gfs2_fsck, skip " \
				     "the rest of this pass or continue " \
				     "(a/s/c)?", "asc");
	if(tolower(response) == 's') {
		skip_this_pass = TRUE;
		return;
	}
	else if (tolower(response) == 'a') {
		fsck_abort = TRUE;
		return;
	}
}

/* Check system inode and verify it's marked "in use" in the bitmap:       */
/* Should work for all system inodes: root, master, jindex, per_node, etc. */
int check_system_inode(struct gfs2_inode *sysinode, const char *filename,
		       void builder(struct gfs2_sbd *sbp),
		       enum gfs2_mark_block mark)
{
	uint64_t iblock = 0;
	struct dir_status ds = {0};

	log_info("Checking system inode '%s'\n", filename);
	if (sysinode) {
		/* Read in the system inode, look at its dentries, and start
		 * reading through them */
		iblock = sysinode->i_di.di_num.no_addr;
		log_info("System inode for '%s' is located at block %"
			 PRIu64 " (0x%" PRIx64 ")\n", filename,
			 iblock, iblock);
		
		/* FIXME: check this block's validity */

		if(gfs2_block_check(bl, iblock, &ds.q)) {
			log_crit("Can't get %s inode block %" PRIu64 " (0x%"
				 PRIx64 ") from block list\n", filename,
				 iblock, iblock);
			return -1;
		}
		/* If the inode exists but the block is marked      */
		/* free, we might be recovering from a corrupt      */
		/* bitmap.  In that case, don't rebuild the inode.  */
		/* Just reuse the inode and fix the bitmap.         */
		if (ds.q.block_type == gfs2_block_free) {
			log_info("The inode exists but the block is not marked 'in use'; fixing it.\n");
			gfs2_block_set(bl, sysinode->i_di.di_num.no_addr,
				       mark);
			ds.q.block_type = mark;
			if (mark == gfs2_inode_dir)
				add_to_dir_list(sysinode->i_sbd,
						sysinode->i_di.di_num.no_addr);
		}
	}
	else
		log_info("System inode for '%s' is missing.\n", filename);
	/* If there are errors with the inode here, we need to
	 * create a new inode and get it all setup - of course,
	 * everything will be in lost+found then, but we *need* our
	 * system inodes before we can do any of that. */
	if(!sysinode || ds.q.block_type != mark) {
		log_err("Invalid or missing %s system inode.\n", filename);
		if (query(&opts, "Create new %s system inode? (y/n) ",
			  filename)) {
			builder(sysinode->i_sbd);
			gfs2_block_set(bl, sysinode->i_di.di_num.no_addr,
				       mark);
			ds.q.block_type = mark;
			if (mark == gfs2_inode_dir)
				add_to_dir_list(sysinode->i_sbd,
						sysinode->i_di.di_num.no_addr);
		}
		else {
			log_err("Cannot continue without valid %s inode\n",
				filename);
			return -1;
		}
	}

	return 0;
}

int check_system_inodes(struct gfs2_sbd *sdp)
{
	/*******************************************************************
	 *******  Check the system inode integrity             *************
	 *******************************************************************/
	if (check_system_inode(sdp->master_dir, "master", build_master,
			       gfs2_inode_dir)) {
		stack;
		return -1;
	}
	if (check_system_inode(sdp->md.rooti, "root", build_root,
			       gfs2_inode_dir)) {
		stack;
		return -1;
	}
	if (check_system_inode(sdp->md.inum, "inum", build_inum,
			       gfs2_inode_file)) {
		stack;
		return -1;
	}
	if (check_system_inode(sdp->md.statfs, "statfs", build_statfs,
			       gfs2_inode_file)) {
		stack;
		return -1;
	}
	if (check_system_inode(sdp->md.jiinode, "jindex", build_jindex,
			       gfs2_inode_dir)) {
		stack;
		return -1;
	}
	if (check_system_inode(sdp->md.riinode, "rindex", build_rindex,
			       gfs2_inode_file)) {
		stack;
		return -1;
	}
	if (check_system_inode(sdp->md.qinode, "quota", build_quota,
			       gfs2_inode_file)) {
		stack;
		return -1;
	}
	if (check_system_inode(sdp->md.pinode, "per_node", build_per_node,
			       gfs2_inode_dir)) {
		stack;
		return -1;
	}
	return 0;
}

int main(int argc, char **argv)
{
	struct gfs2_sbd sb;
	struct gfs2_sbd *sbp = &sb;
	int j;
	enum update_flags update_sys_files;

	memset(sbp, 0, sizeof(*sbp));

	if(read_cmdline(argc, argv, &opts))
		return 1;
	setbuf(stdout, NULL);
	log_notice("Initializing fsck\n");
	if (initialize(sbp))
		return 1;

	signal(SIGINT, interrupt);
	log_notice("Starting pass1\n");
	pass = "pass 1";
	last_reported_block = 0;
	if (pass1(sbp))
		return 1;
	if (skip_this_pass || fsck_abort) {
		skip_this_pass = FALSE;
		log_notice("Pass1 interrupted   \n");
	}
	else
		log_notice("Pass1 complete      \n");

	/* Make sure the system inodes are okay & represented in the bitmap. */
	check_system_inodes(sbp);

	if (!fsck_abort) {
		last_reported_block = 0;
		pass = "pass 1b";
		log_notice("Starting pass1b\n");
		if(pass1b(sbp))
			return 1;
		if (skip_this_pass || fsck_abort) {
			skip_this_pass = FALSE;
			log_notice("Pass1b interrupted   \n");
		}
		else
			log_notice("Pass1b complete\n");
	}
	if (!fsck_abort) {
		last_reported_block = 0;
		pass = "pass 1c";
		log_notice("Starting pass1c\n");
		if(pass1c(sbp))
			return 1;
		if (skip_this_pass || fsck_abort) {
			skip_this_pass = FALSE;
			log_notice("Pass1c interrupted   \n");
		}
		else
			log_notice("Pass1c complete\n");
	}
	if (!fsck_abort) {
		last_reported_block = 0;
		pass = "pass 2";
		log_notice("Starting pass2\n");
		if (pass2(sbp))
			return 1;
		if (skip_this_pass || fsck_abort) {
			skip_this_pass = FALSE;
			log_notice("Pass2 interrupted   \n");
		}
		else
			log_notice("Pass2 complete      \n");
	}
	if (!fsck_abort) {
		last_reported_block = 0;
		pass = "pass 3";
		log_notice("Starting pass3\n");
		if (pass3(sbp))
			return 1;
		if (skip_this_pass || fsck_abort) {
			skip_this_pass = FALSE;
			log_notice("Pass3 interrupted   \n");
		}
		else
			log_notice("Pass3 complete      \n");
	}
	if (!fsck_abort) {
		last_reported_block = 0;
		pass = "pass 4";
		log_notice("Starting pass4\n");
		if (pass4(sbp))
			return 1;
		if (skip_this_pass || fsck_abort) {
			skip_this_pass = FALSE;
			log_notice("Pass4 interrupted   \n");
		}
		else
			log_notice("Pass4 complete      \n");
	}
	if (!fsck_abort) {
		last_reported_block = 0;
		pass = "pass 5";
		log_notice("Starting pass5\n");
		if (pass5(sbp))
			return 1;
		if (skip_this_pass || fsck_abort) {
			skip_this_pass = FALSE;
			log_notice("Pass5 interrupted   \n");
		}
		else
			log_notice("Pass5 complete      \n");
	}
	update_sys_files = (opts.no ? not_updated : updated);
	/* Free up our system inodes */
	inode_put(sbp->md.inum, update_sys_files);
	inode_put(sbp->md.statfs, update_sys_files);
	for (j = 0; j < sbp->md.journals; j++)
		inode_put(sbp->md.journal[j], update_sys_files);
	inode_put(sbp->md.jiinode, update_sys_files);
	inode_put(sbp->md.riinode, update_sys_files);
	inode_put(sbp->md.qinode, update_sys_files);
	inode_put(sbp->md.pinode, update_sys_files);
	inode_put(sbp->md.rooti, update_sys_files);
	inode_put(sbp->master_dir, update_sys_files);
	if (lf_dip)
		inode_put(lf_dip, update_sys_files);

	if (!opts.no)
		log_notice("Writing changes to disk\n");
	bsync(sbp);
	destroy(sbp);
	log_notice("gfs2_fsck complete    \n");

	return 0;
}
