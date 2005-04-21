/*
  Copyright Red Hat, Inc. 2003-2004

  The Magma Cluster API Library is free software; you can redistribute
  it and/or modify it under the terms of the GNU Lesser General Public
  License as published by the Free Software Foundation; either version
  2.1 of the License, or (at your option) any later version.

  The Magma Cluster API Library is distributed in the hope that it will
  be useful, but WITHOUT ANY WARRANTY; without even the implied warranty
  of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  Lesser General Public License for more details.

  You should have received a copy of the GNU Lesser General Public
  License along with this library; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307
  USA.
 */
/** @file
 * Connection list handling routines.
 */
#include <sys/queue.h>
#include <pthread.h>
#include <sys/select.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <stdint.h>
#include <magmamsg.h>
#include <stdio.h>
#include <unistd.h>


/**
 * Node in connection list.
 */
typedef struct _conn_node {
	TAILQ_ENTRY(_conn_node) cn_entries;	/**< sys/queue tailq entri */
	int	 cn_fd;			/**< File descriptor */
	int	 cn_flags;		/**< Info about file descriptor */
	int	 cn_purpose;		/**< Application-specific purpose */
} conn_node_t;

typedef TAILQ_HEAD(_conn_list_head, _conn_node) conn_list_head_t;

static pthread_mutex_t conn_list_mutex = PTHREAD_MUTEX_INITIALIZER;
static conn_list_head_t _conn_list_head = { NULL,
					    &(_conn_list_head.tqh_first) };

static inline conn_node_t *locate_node(conn_list_head_t *, int fd);

static inline int clist_delete_nt(conn_list_head_t *, int fd);


/**
 * Insert a file descriptor with the given flags into our list.
 *
 * @param fd		File descriptor to add
 * @param flags		Flags.  Application specific.
 * @see			fdlist_add
 * @return		0
 */
int
clist_insert_nt(conn_list_head_t *conn_list_head, int fd, int flags)
{
	conn_node_t *node;

	node = malloc(sizeof(*node));
	/* ASSERT(node); */
	memset(node,0,sizeof(*node));

	node->cn_fd = fd;
	node->cn_flags = flags;
	node->cn_purpose = 0;

	clist_delete_nt(conn_list_head, fd);
	TAILQ_INSERT_HEAD(conn_list_head, node, cn_entries);

	return 0;
}


int
clist_insert(int fd, int flags)
{
	int ret;
	pthread_mutex_lock(&conn_list_mutex);
	ret = clist_insert_nt(&_conn_list_head, fd, flags);
	pthread_mutex_unlock(&conn_list_mutex);
	return ret;
}


static inline int
clist_delete_nt(conn_list_head_t *conn_list_head, int fd)
{
	conn_node_t *curr;

	if ((curr = locate_node(conn_list_head, fd))) {
		TAILQ_REMOVE(conn_list_head, curr, cn_entries);
		free(curr);
		return 0;
	}

	return 1;
}


/**
 * Delete a file descriptor from our connection list.
 *
 * @param fd		The file descriptor to delete.
 * @return		1 if not found, 0 if successful.
 */
int
clist_delete(int fd)
{
	int rv;

	pthread_mutex_lock(&conn_list_mutex);
	rv = clist_delete_nt(&_conn_list_head, fd);
	pthread_mutex_unlock(&conn_list_mutex);

	return rv;
}


void
clist_purgeall_nt(conn_list_head_t *conn_list_head)
{
	conn_node_t *curr;
	while ((curr = conn_list_head->tqh_first)) {
		TAILQ_REMOVE(conn_list_head, curr, cn_entries);
		close(curr->cn_fd);
		free(curr);
	}
}


/**
  Purge all entries in the connection list
 */
void
clist_purgeall(void)
{
	pthread_mutex_lock(&conn_list_mutex);
	clist_purgeall_nt(&_conn_list_head);
	pthread_mutex_unlock(&conn_list_mutex);
}


/**
 * Set all file descriptors in the connection list in a given fd_set.
 * We close any file descriptors which have gone bad for any reason and remove
 * them from our list.
 *
 * @param set		The file descriptor set to modify.
 * @param flags		Flags to look for.
 * @param purpose	User or application-defined purpose to look for.
 * @return		Max file descriptor to pass to select.
 */
inline int
clist_fill_fdset_nt(conn_list_head_t *conn_list_head, fd_set *set, int flags,
		    int purpose)
{
	conn_node_t *curr;
	fd_set test_fds;
	struct timeval tv;
	int max = -1;

top:
	for (curr = conn_list_head->tqh_first; curr;
	     curr = curr->cn_entries.tqe_next) {

		if (flags && ((curr->cn_flags & flags) != flags))
			continue;

		if ((purpose != MSGP_ALL) && (curr->cn_purpose != purpose))
			continue;

		FD_ZERO(&test_fds);
		FD_SET(curr->cn_fd, &test_fds);
		tv.tv_sec = 0;
		tv.tv_usec = 0;
		if (select(curr->cn_fd + 1, &test_fds, &test_fds, NULL,
			   &tv) == -1) {
			if (errno == EBADF || errno == EINVAL) {
				clist_delete_nt(conn_list_head, curr->cn_fd);
				goto top;
			}
		}

		if (curr->cn_fd >= max)
			max = curr->cn_fd;
		
		FD_SET(curr->cn_fd, set);
	}


	return max;
}


int
clist_fill_fdset(fd_set *set, int flags, int purpose)
{
	int ret;

	pthread_mutex_lock(&conn_list_mutex);
	ret = clist_fill_fdset_nt(&_conn_list_head, set, flags, purpose);
	pthread_mutex_unlock(&conn_list_mutex);
	return ret;
}

/**
  Locate a connection node.

  @param fd		File descriptor to locate
  @return		Connection node correspond to fd or NULL
 */
static inline conn_node_t *
locate_node(conn_list_head_t *conn_list_head, int fd)
{
	conn_node_t *curr;

	for (curr = conn_list_head->tqh_first; curr;
	     curr = curr->cn_entries.tqe_next) {

		if (curr->cn_fd == fd) {
			/* Move FD to front of list; lru file descriptors
			   move to end.  */
			TAILQ_REMOVE(conn_list_head, curr, cn_entries);
			TAILQ_INSERT_HEAD(conn_list_head, curr, cn_entries);
			return curr;
		}
	}

	return NULL;
}


/**
 * Determine the next set file descriptor in our connection list, given a 
 * set of file descriptors.  O(n), but works.
 *
 * @param set		File descriptor set to check into.
 * @return		-1 on failure or the number of the next set file
 *			descriptor if successful.
 */
inline int
clist_next_set_nt(conn_list_head_t *conn_list_head, fd_set *set)
{
	int rv;
	conn_node_t *curr;


	for (curr = conn_list_head->tqh_first; curr;
	     curr = curr->cn_entries.tqe_next) {

		if (FD_ISSET(curr->cn_fd, set)) {
			FD_CLR(curr->cn_fd, set);
			rv = curr->cn_fd;
			return rv;
		}
	}


	return -1;
}


int
clist_next_set(fd_set *set)
{
	int ret;
	pthread_mutex_lock(&conn_list_mutex);
	ret = clist_next_set_nt(&_conn_list_head, set);
	pthread_mutex_unlock(&conn_list_mutex);
	return ret;
}


/**
 * Set a given file descriptor's purpose.
 *
 * @param fd		File descriptor.
 * @param purpose	Application specific purpose ID
 * @return		-1 if not found, or 0 on success.
 */
inline int
clist_set_purpose_nt(conn_list_head_t *conn_list_head, int fd, int purpose)
{
	int rv = -1;
	conn_node_t *curr;

	if ((curr = locate_node(conn_list_head, fd))) {
		curr->cn_purpose = purpose;
		rv = 0;
	}

	return rv;
}


int
clist_set_purpose(int fd, int purpose)
{
	int rv;
	pthread_mutex_lock(&conn_list_mutex);
	rv = clist_set_purpose_nt(&_conn_list_head, fd, purpose);
	pthread_mutex_unlock(&conn_list_mutex);
	return rv;
}


/**
 * Get a given file descriptor's purpose.
 *
 * @param fd		File descriptor.
 * @return		-1 if not found, the purpose id.
 */
inline int
clist_get_purpose_nt(conn_list_head_t *conn_list_head, int fd)
{
	int rv = -1;
	conn_node_t *curr;

	if ((curr = locate_node(conn_list_head, fd)))
		rv = curr->cn_purpose;

	return rv;
}


int
clist_get_purpose(int fd)
{
	int rv;
	pthread_mutex_lock(&conn_list_mutex);
	rv = clist_get_purpose_nt(&_conn_list_head, fd);
	pthread_mutex_unlock(&conn_list_mutex);
	return rv;
}


/**
 * Get a given file descriptor's purpose.
 *
 * @param fd		File descriptor.
 * @return		-1 if not found, the purpose id.
 */
inline int
clist_get_flags_nt(conn_list_head_t *conn_list_head, int fd)
{
	int rv = 0;
	conn_node_t *curr;

	if ((curr = locate_node(conn_list_head, fd)))
		rv = curr->cn_flags;

	return rv;
}


int
clist_get_flags(int fd)
{
	int rv;
	pthread_mutex_lock(&conn_list_mutex);
	rv = clist_get_flags_nt(&_conn_list_head, fd);
	pthread_mutex_unlock(&conn_list_mutex);
	return rv;
}


/**
  Dump our list to stdout
 */

#define printifflag(flag) if (curr->cn_flags & flag) printf(" " #flag)
void
clist_dump_nt(conn_list_head_t *conn_list_head)
{
	conn_node_t *curr;

	for (curr = conn_list_head->tqh_first; curr;
	     curr = curr->cn_entries.tqe_next) {

		printf("File Descriptor %d:\n", curr->cn_fd);
		if (curr->cn_flags) {
			printf("* Flags: 0x%08x", curr->cn_flags);

			printifflag(MSG_OPEN);
			printifflag(MSG_LISTEN);
			printifflag(MSG_CONNECTED);
			printifflag(MSG_WRITE);
			printifflag(MSG_READ);

			printf("\n");
		}

		if (curr->cn_purpose != -1)
			printf("* Purpose ID: %d\n", curr->cn_purpose);
		printf("\n");
	}

}


void
clist_dump(void)
{
	pthread_mutex_lock(&conn_list_mutex);
	clist_dump_nt(&_conn_list_head);
	pthread_mutex_unlock(&conn_list_mutex);
}
