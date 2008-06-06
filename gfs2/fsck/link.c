#include <inttypes.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>

#include "libgfs2.h"
#include "fsck.h"
#include "inode_hash.h"
#include "link.h"

int set_link_count(struct gfs2_sbd *sbp, uint64_t inode_no, uint32_t count)
{
	struct inode_info *ii = NULL;
	log_debug("Setting link count to %u for %" PRIu64 " (0x%" PRIx64 ")\n",
			  count, inode_no, inode_no);
	/* If the list has entries, look for one that matches
	 * inode_no */
	ii = inode_hash_search(inode_hash, inode_no);
	if(ii) {
		if(ii->link_count) {
			log_err("Link count already set for inode #%" PRIu64 " (0x%"
					PRIx64 ")!\n", inode_no, inode_no);
			stack;
			return -1;
		}
		else
			ii->link_count = count;
	}
	else {
		/* If not match was found, add a new entry and set it's
		 * link count to count*/
		if(!(ii = (struct inode_info *) malloc(sizeof(*ii)))) {
			log_err("Unable to allocate inode_info structure\n");
			stack;
			return -1;
		}
		memset(ii, 0, sizeof(*ii));
		ii->inode = inode_no;
		ii->link_count = count;
		inode_hash_insert(inode_hash, inode_no, ii);
	}
	return 0;
}

int increment_link(struct gfs2_sbd *sbp, uint64_t inode_no)
{
	struct inode_info *ii = NULL;

	ii = inode_hash_search(inode_hash, inode_no);
	/* If the list has entries, look for one that matches
	 * inode_no */
	if(ii) {
		ii->counted_links++;
		log_debug("Incremented counted links to %u for %"PRIu64" (0x%"
				  PRIx64 ")\n", ii->counted_links, inode_no, inode_no);
		return 0;
	}
	log_debug("No match found when incrementing link for %" PRIu64
			  " (0x%" PRIx64 ")!\n", inode_no, inode_no);
	/* If no match was found, add a new entry and set its
	 * counted links to 1 */
	if(!(ii = (struct inode_info *) malloc(sizeof(*ii)))) {
		log_err("Unable to allocate inode_info structure\n");
		stack;
		return -1;
	}
	if(!memset(ii, 0, sizeof(*ii))) {
		log_err("Unable to zero inode_info structure\n");
		stack;
		return -1;
	}
	ii->inode = inode_no;
	ii->counted_links = 1;
	inode_hash_insert(inode_hash, inode_no, ii);

	return 0;
}

int decrement_link(struct gfs2_sbd *sbp, uint64_t inode_no)
{
	struct inode_info *ii = NULL;

	ii = inode_hash_search(inode_hash, inode_no);
	/* If the list has entries, look for one that matches
	 * inode_no */
	log_err("Decrementing %"PRIu64" (0x%" PRIx64 ")\n", inode_no, inode_no);
	if(ii) {
		ii->counted_links--;
		return 0;
	}
	log_debug("No match found when decrementing link for %" PRIu64
			  " (0x%" PRIx64 ")!\n", inode_no, inode_no);
	return -1;

}


