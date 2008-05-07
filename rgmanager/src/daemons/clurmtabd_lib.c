/*
  Copyright Red Hat, Inc. 2002-2003

  This program is free software; you can redistribute it and/or modify it
  under the terms of the GNU General Public License as published by the
  Free Software Foundation; either version 2, or (at your option) any
  later version.

  This program is distributed in the hope that it will be useful, but
  WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; see the file COPYING.  If not, write to the
  Free Software Foundation, Inc.,  675 Mass Ave, Cambridge, 
  MA 02139, USA.
*/
/** @file
 * Implements rmtab read/write/list handling functions utilized by
 * clurmtabd.
 *
 * Author: Lon H. Hohberger <lhh at redhat.com>
 *
 * This was written for two reasons:
 * (1) The nfs-utils code was difficult to adapt to the requirements, and
 * (2) to prevent cross-breeding of nfs-utils with clumanager.
 *
 * So, in a sense, this is a re-invention of the wheel, but it keeps the
 * pacakges separate, thus easing maintenance by lessening the number of
 * patches and code forks required for Enterprise Linux.
 */
#include <platform.h>
#include <stdlib.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/param.h>
#include <unistd.h>
#include <rmtab.h>
#include <libgen.h>

/*
 * Function Prototypes
 */
static int fp_lock(FILE *fp, int type);
static int fp_unlock(FILE *fp);
static inline void un_diffize(rmtab_node *dest, rmtab_node *src);


/*
 * free an rmtab node
 */
void
rmtab_free(rmtab_node *ptr)
{
	if(ptr->rn_hostname)
		free(ptr->rn_hostname);
	if(ptr->rn_path)
		free(ptr->rn_path);
	free(ptr);
}


/*
 * take advisory lock.  Retry until we're blue in the face.
 */
static int
fp_lock(FILE *fp, int type)
{
	int fd;
	struct flock flock;
	
	fd = fileno(fp);
	memset(&flock,0,sizeof(flock));

	/* Lock. */
	flock.l_type = type;
	while (fcntl(fd, F_SETLK, &flock) == -1) {
		if ((errno != EAGAIN) && (errno != EACCES))
			return -1;

		usleep(10000);
	}

	return 0;
}


/*
 * unlock...
 */
static int
fp_unlock(FILE *fp)
{
	int fd = fileno(fp);
	struct flock flock;

	memset(&flock,0,sizeof(flock));
	flock.l_type = F_UNLCK;
	return fcntl(fd, F_SETLK, &flock);
}


/*
 * Inserts a node into the list in ASCII-sorted order; rnew must have been
 * a user-allocated chunk of memory. (static = big nono)
 */
int
__rmtab_insert(rmtab_node **head, rmtab_node *rnew)
{
	rmtab_node *back, *curr;
	int rv = 1;

	/* insert as first entry */
	if (!(*head) || (rv = rmtab_cmp_min(rnew, *head)) < 0) {
		rnew->rn_next = *head;
		*head = rnew;
		return 0;
	}

	/* Duplicate match? */
	if (!rv) {
		if ((*head)->rn_count < rnew->rn_count)
			(*head)->rn_count = rnew->rn_count;
		return 1;
	}

	/* Standard insert - not beginning, not end. */
	back = NULL;
	curr = *head;
	while (curr) {
		/* Insert before current */
		if ((rv = rmtab_cmp_min(rnew, curr)) < 0) {
			back->rn_next = rnew;
			rnew->rn_next = curr;
			return 0;
		}

		/* Duplicate match? Snag the greater count. */
		if (!rv) {
			if (curr->rn_count < rnew->rn_count)
				curr->rn_count = rnew->rn_count;
			return 0;
		}

		/* Loopy loopy */
		back = curr;
		curr = curr->rn_next;
	}

	/* Tack on at end of list */
	back->rn_next = rnew;

	return 0;
}


/*
 * Inserts a node into the list in ASCII-sorted order; rnew must have been
 * a user-allocated chunk of memory. (static = big nono)
 */
int
__rmtab_insert_after(rmtab_node *pre, rmtab_node *rnew)
{
	rmtab_node *back, *curr;
	int rv = 1;

	/* insert as first entry */
	if ((rv = rmtab_cmp_min(rnew, pre)) < 0) {
		return -1;
	}

	/* Duplicate match? */
	if (!rv) {
		if (pre->rn_count < rnew->rn_count)
			pre->rn_count = rnew->rn_count;
		return 1;
	}

	/* Standard insert - not beginning, not end. */
	back = NULL;
	curr = pre;
	while (curr) {
		/* Insert before current */
		if ((rv = rmtab_cmp_min(rnew, curr)) < 0) {
			back->rn_next = rnew;
			rnew->rn_next = curr;
			return 0;
		}

		/* Duplicate match? Snag the greater count. */
		if (!rv) {
			if (curr->rn_count < rnew->rn_count)
				curr->rn_count = rnew->rn_count;
			return 0;
		}

		/* Loopy loopy */
		back = curr;
		curr = curr->rn_next;
	}

	/* Tack on at end of list */
	back->rn_next = rnew;

	return 0;
}


/*
 * Inserts (host,path) rmtab into a list pointed to by **head
 */
rmtab_node *
rmtab_insert(rmtab_node **head, rmtab_node *pre, char *host,
	     char *path, int count)
{
	rmtab_node *rnew;

	/* simple bounds-checking */
	if (!head || !host || !path || !strlen(host) || !strlen(path))
		return NULL;

	/* Copy in info */
	rnew = malloc(sizeof(*rnew));
	memset(rnew, 0, sizeof(*rnew));
	rnew->rn_hostname = strdup(host);
	rnew->rn_path = strdup(path);
	rnew->rn_count = count;

	if (pre) {
		/* We got a preceding node... try to insert */
		switch(__rmtab_insert_after(pre, rnew)) {
		case -1:
			/* Failed insert after... try before? */
			break;
		case 0:
			goto out;
		case 1:
			rmtab_free(rnew);
			rnew = NULL;
			goto out;
		}
	}


	/* Insert into our list officially */
	if (__rmtab_insert(head, rnew) == 1) {
		/* Duplicate match */
		rmtab_free(rnew);
		rnew = NULL;
	}

out:
	return rnew;
}


/*
 * removes a node based on contents of *entry 
 * user must free memory, if applicable.
 */
rmtab_node *
__rmtab_remove(rmtab_node **head, rmtab_node *entry)
{
	int rv = -1;
	rmtab_node *curr, *back;

	back = NULL; curr = *head;

	for (curr = *head, back = NULL; 
	     (rv = rmtab_cmp_min(curr, entry)) < 0;
	     back = curr, curr = curr->rn_next);

	/* overshot => no match */
	if (rv != 0)
		return NULL;

	if (back) {
		back->rn_next = curr->rn_next;
		curr->rn_next = NULL;
		return curr;
	}

	/* no back pointer = first node in list. */
	back = curr;
	*head = curr->rn_next;
	back->rn_next = NULL;
	return back;
}


/*
 * removes an entry in the list; user must free.
 */
rmtab_node *
rmtab_remove(rmtab_node **head, char *host, char *path)
{
	rmtab_node tmp;
	rmtab_node *ret;

	/* Wrappers, wrappers */
	memset(&tmp, 0, sizeof(tmp));
	if (host)
		tmp.rn_hostname = strdup(host);
	if (path)
		tmp.rn_path = strdup(path);

	ret = __rmtab_remove(head, &tmp);
	if (tmp.rn_hostname)
		free(tmp.rn_hostname);
	if (tmp.rn_path)
		free(tmp.rn_path);

	return ret;
}


/*
 * Frees an entire rmtab list.
 */
void
rmtab_kill(rmtab_node **head)
{
	rmtab_node *curr, *back;

	if (!head || !*head)
		return;

	curr = *head; back = NULL;

	while (curr) {
		if (back)
			rmtab_free(back);
		back = curr;
		curr = curr->rn_next;
	}

	if (back)
		rmtab_free(back);

	*head = NULL;
}


/*
 * Finds the differences between two rmtabs.  This is generally called when
 * we read our file locally.  The diff outputs in **diff are collapsed
 * noted as '<' and '>' before the hostname, eg
 *
 * <boris /tmp 1
 * >dragracer /tmp2 1
 */
int
rmtab_diff(rmtab_node *old, rmtab_node *new, rmtab_node **diff)
{
	rmtab_node *old_curr, *new_curr, *last = NULL;
	int rv;
	char buf[MAXHOSTNAMELEN];

	if (!diff)
		return -1;

	old_curr = old;
	new_curr = new;

	/* This loop will exit when the first list is exhausted. */
	while (old_curr && new_curr) {

		/* Entries the same. */
		if (!(rv = rmtab_cmp(old_curr, new_curr))) {
			old_curr = old_curr->rn_next;
			new_curr = new_curr->rn_next;
			continue;
		}

		/* Old < new = deleted entry */
		if (rv < 0) {
			snprintf(buf, sizeof(buf), "<%s",
				 old_curr->rn_hostname);
			last = rmtab_insert(diff, last, buf, old_curr->rn_path,
			       		    old_curr->rn_count);
			old_curr = old_curr->rn_next;
			continue;
		}

		/* old > new = new entry */
		snprintf(buf, sizeof(buf), ">%s", new_curr->rn_hostname);
		last = rmtab_insert(diff, last, buf, new_curr->rn_path,
			    	    new_curr->rn_count);
		new_curr = new_curr->rn_next;
	}

	/* Meaning only one of the two following loops actually is executed:*/

	/* Add remaining stuff in 'old' to 'deleted' list */
	for (;old_curr; old_curr = old_curr->rn_next) {
		snprintf(buf, sizeof(buf), "<%s", old_curr->rn_hostname);
		last = rmtab_insert(diff, last, buf, old_curr->rn_path,
			    	    old_curr->rn_count);
	}

	/* Add remaining stuff in 'new' to 'added' list. */
	for (;new_curr; new_curr = new_curr->rn_next) {
		snprintf(buf, sizeof(buf), ">%s", new_curr->rn_hostname);
		last = rmtab_insert(diff, last, buf, new_curr->rn_path,
			    	    new_curr->rn_count);
	}

	return 0;
}


/*
 * strips the "diff" character ('>' || '<') from the beginning of the hostname
 * in *src and stores the resulting stuff in *dest.
 */
static inline void
un_diffize(rmtab_node *dest, rmtab_node *src)
{
	dest->rn_hostname = strdup(&src->rn_hostname[1]);
	dest->rn_path = strdup(src->rn_path);
	dest->rn_count = src->rn_count;
	dest->rn_next = NULL;
}


/*
 * Merges 'patch' list with **head.
 * Modifies list in **head; leaves *patch unchanged.  This is generally
 * called when we receive an update from a peer.
 */
int
rmtab_merge(rmtab_node **head, rmtab_node *patch)
{
	rmtab_node *curr, *oldp = NULL, *last = NULL;
	rmtab_node tmpnode;
	int rv = -1;

	/* Delete all matching entries */
	for (curr = patch; curr; curr = curr->rn_next) {

		un_diffize(&tmpnode, curr);

		if (curr->rn_hostname[0] == '<') {
			if ((oldp = __rmtab_remove(head, &tmpnode))) {
				rmtab_free(oldp);
				oldp = NULL;
			}
		} else if (curr->rn_hostname[0] == '>') {
			if ((last = rmtab_insert(head, last,
						 tmpnode.rn_hostname,
						 tmpnode.rn_path,
						 tmpnode.rn_count)) == NULL) {
				rv = -1;
				break;
			}
		} else {
			/* EEEEEKKKK */
			rv = -1;
			break;
		}

		/*
		 * Free anything we allocated
		 */
		if (tmpnode.rn_hostname) {
			free(tmpnode.rn_hostname);
			tmpnode.rn_hostname = NULL;
		}
		if (tmpnode.rn_path) {
			free(tmpnode.rn_path);
			tmpnode.rn_path = NULL;
		}
	}

	if (tmpnode.rn_hostname)
		free(tmpnode.rn_hostname);
	if (tmpnode.rn_path)
		free(tmpnode.rn_path);

	return 0;
}


/*
 * Builds a list by parsing an rmtab in *fp... pretty simple
 */
int
rmtab_import(rmtab_node **head, FILE *fp)
{
	int n = 0;
	char line[MAXPATHLEN];
	char *hostname, *path, *cnt;
	int count = 0;
	rmtab_node *last = NULL;

	if (!fp || !head)
		return 0;

	if (fp_lock(fp, F_RDLCK) == -1) {
		perror("fplock");
		return -1;
	}

	while (fgets(line,sizeof(line),fp) != NULL) {
		hostname = strtok(line, ":");
		path = strtok(NULL, ":");

		/* mount count corresponding to the entry */
		cnt = strtok(NULL, ":");
		if (cnt)
			count = strtol(cnt, NULL, 0);

		if (!hostname || !path || !count)
			/* End - malformed last line */
			break;

		if ((last = rmtab_insert(head, last, hostname, path,
	 				 count)) != NULL)
			n++;
	}

	fp_unlock(fp);
	return n;
}


/*
 * Writes rmtab to export file *fp
 */
int
rmtab_export(rmtab_node *head, FILE *fp)
{
	rmtab_node *curr;

	/*
	 * lhh - fix - we removed the check for !head, because we _CAN_ have 
	 * an empty /var/lib/nfs/rmtab!!
	 */
	if (!fp)
		return -1;

	if (fp_lock(fp, F_WRLCK) == -1)
		return -1;

	for (curr = head; curr; curr = curr->rn_next)
		fprintf(fp,"%s:%s:0x%08x\n",curr->rn_hostname, curr->rn_path,
			curr->rn_count);

	fp_unlock(fp);
	return 0;
}


/*
 *
 */
int
rmtab_read(rmtab_node **head, char *filename)
{
	FILE *fp;
	int rv = 0;
	int esave = 0;

        if (!filename || !strlen(filename))
		return -1;
    
	fp = fopen(filename, "r");
	if (!fp) {
		/* It's ok if it's not there. */
		if (errno == ENOENT) {
			close(open(filename, O_WRONLY|O_SYNC|O_CREAT, S_IRUSR | S_IWUSR));
			return 0;
		}
		perror("fopen");
		return -1;
	}

	rv = rmtab_import(head, fp);
	esave = errno;

	fclose(fp);
	errno = esave;
	return rv;
}


/*
 * Writes rmtab list in *head to *filename; in an atomic fashion
 */
int
rmtab_write_atomic(rmtab_node *head, char *filename)
{
	char tmpfn[MAXPATHLEN],
	     oldfn[MAXPATHLEN],
	     realfn[MAXPATHLEN];
	FILE *fp;
	int len, tfd, ofd;
	int rv = -1;

	memset(realfn, 0, sizeof(realfn));
	memset(oldfn, 0, sizeof(oldfn));
	memset(tmpfn, 0, sizeof(tmpfn));

	len = strlen(filename);
	if (len > (MAXPATHLEN - 1))
		len = MAXPATHLEN - 1;

	memcpy(realfn, filename, len);

	/* chop off the end - GLIBC modifies the argument here */
	dirname(realfn);

	snprintf(oldfn, sizeof(oldfn), "%s/tmp.XXXXXX", realfn);
	snprintf(tmpfn, sizeof(tmpfn), "%s/tmp.XXXXXX", realfn);

	if ((ofd = mkstemp(oldfn)) == -1)
		return -1;
	if ((tfd = mkstemp(tmpfn)) == -1) {
		close(ofd);
		unlink(oldfn);
		return -1;
	}

	/* Kill the file so we can link it (else it'll not work) */
	close(ofd);
	unlink(oldfn);

	/* set up the link */
	if (link(filename, oldfn) == -1)
		goto fail;

	/* Export the file.  Yes, we have two fd's on it now.  That's ok :) */
	if (!(fp = fopen(tmpfn, "w")))
		goto fail;

	if (rmtab_export(head, fp) == -1)
		goto fail;

	fsync(fileno(fp));
	fclose(fp);

	/* atomic update */
	if (rename(tmpfn, filename) == -1)
		goto fail;

	if (unlink(oldfn) == -1)
		goto fail;

	rv = 0;
fail:
	close(tfd);

	return rv;
}


/*
 * Well, 1106 bytes per transaction is a bit much, especially for 
 * busy nfs clusters... so, we invent 'compact', which simply eliminates
 * all dead data.  We basically have two strings + their trailing NULLs,
 * and one int (the mount count)
 */
size_t
rmtab_pack_size(rmtab_node *head)
{
	rmtab_node *curr;
	size_t total = 0;

	for (curr = head; curr; curr = curr->rn_next) {
		/* leave space for NULLs */
		total += strlen(curr->rn_hostname) + 1;
		total += strlen(curr->rn_path) + 1;
		total += sizeof(curr->rn_count); /* uint32_t */
	}

	return total;
}


/*
 * rmtab_pack
 *
 * stores the list in *head in *buf - packed as much as possible without
 * actually employing compression.
 *
 * host.domain.com\0/usr/src\0####host2.domain.com\0/usr/src\0####
 */
int
rmtab_pack(char *buf, rmtab_node *head)
{
	int n = 0;
	char *bptr = buf;
	rmtab_node *curr;
	size_t len;
	uint32_t c_tmp;
	
	/* Pass 2 */
	for (curr = head; curr; curr = curr->rn_next) {
		/* Hostname */
		len = strlen(curr->rn_hostname);
		memcpy(bptr, curr->rn_hostname, len);
		bptr[len] = 0;
		bptr += (len + 1); /* space for the NULL */

		/* path */
		len = strlen(curr->rn_path);
		memcpy(bptr, curr->rn_path, len);
		bptr[len] = 0;
		bptr += (len + 1);

		/* count */
		len = sizeof(curr->rn_count);

		/* Flip-endianness */
		c_tmp = curr->rn_count; /* uint32_t! */
		swab32(c_tmp);
		
		memcpy(bptr, &c_tmp, len);
		bptr += len;

		n++;
	}

	return n;
}


/*
 * rmtab_unpack
 * .... reverse of rmtab_pack.
 */
int
rmtab_unpack(rmtab_node **head, char *src, size_t srclen)
{
	int n = 0;
	char *hostp, *pathp;
	uint32_t *countp;
	size_t len;
	size_t total = 0;
	rmtab_node *last = NULL;

	if (!src[0])
		return 0;

	hostp = src;

	while (total < srclen) {
		len = strlen(hostp) + 1;
		pathp = hostp + len;

		len += strlen(pathp) + 1;

		/* flip-endianness if require */
		countp = (uint32_t *)(hostp + len);
		swab32(*countp);

		len += sizeof(uint32_t);

		if ((last = rmtab_insert(head, NULL, hostp, pathp,
					 *countp)) == NULL)
			return -1;

		hostp += len;
		total += len;

		n++;
	}

	return n;
}


int
rmtab_cmp_min(rmtab_node *left, rmtab_node *right)
{
	int rv = 0;

	if (!left || !right)
		return ( !!right - !!left );

	if ((rv = strcmp(left->rn_hostname, right->rn_hostname)))
		return rv;

	return (strcmp(left->rn_path, right->rn_path));
}


/*
 * Compares two rmtab_nodes.
 */
int
rmtab_cmp(rmtab_node *left, rmtab_node *right)
{
	int rv;

	if ((rv = rmtab_cmp_min(left,right)))
		return rv;

	if (left->rn_count > right->rn_count)
		return -1;

	return (left->rn_count < right->rn_count);
}


/*
 * Kill an old rmtab in dest and overwrite with *src.
 */
int
rmtab_move(rmtab_node **dest, rmtab_node **src)
{
	rmtab_kill(dest);

	*dest = *src;
	*src = NULL;

	return 0;
}


#ifdef DEBUG
/*
 * prints the contents of **head
 */
int
rmtab_dump(rmtab_node *head)
{
	rmtab_node *curr;
       
	for (curr = head; curr; curr = curr->rn_next)
		printf("%s:%s:0x%08x\n",curr->rn_hostname,curr->rn_path,
		       curr->rn_count);
	return 0;
}
#endif

