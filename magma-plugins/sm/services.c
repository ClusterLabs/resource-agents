/*
  Copyright Red Hat, Inc. 2004

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
#include <stdio.h>
#include <magma.h>
#include <fcntl.h>
#include <cnxman-socket.h>
#include <assert.h>
#include <stdint.h>
#include <linux/ioctl.h>
#include <string.h>
#include <unistd.h>
#include <sys/ioctl.h>

#define BLOCK_SIZE 4096

#define TEST \
"Service          Name                              GID LID State     Code\n" \
"User:            \"Test\"                              6   6 run       -\n" \
"[1 2 3 4 5 6]\n" \
"User:            \"Test2\"                             6   6 run       -\n" \
"[1 2 3]\n" \
"User:            \"Test3\"                             6   6 run       -\n" \
"[2 3]\n"


static uint32_t
__group_member_ids(char *groupname, char *buffer, size_t bufferlen,
		   uint64_t **ids)
{
	int x;
	int state = 0;
	uint32_t ret = 0;
	char *start = NULL;
	char *end = NULL;

	*ids = NULL;
	
	for (x = 0; x < bufferlen ; x++) {

		switch(state) {
		case 0:
			if (buffer[x] == '\n' || buffer[x] == '\r')
				state = 1;
			continue;
		case 1:
			if ((bufferlen - x) < 5)
				return 0;

		    	if (!strncmp(&buffer[x], "User:", 5)) {
				x += 5;
		       		state = 2;	/* User found */
			}
			continue;

		case 2: /* Open quote found */
			if (buffer[x] == '"') {
				state = 3;
			}
			continue;

		case 3: 
			start = &buffer[x];
			state = 4;
			continue;

		case 4: /* Close quote found */
			if (buffer[x] != '"')
				continue;
			end = &buffer[x];

			if ((strlen(groupname) == (end - start)) &&
			    (!strncmp(start, groupname, end - start))) {
				/* Skip group name */
				x += (end - start);
				state = 5;
			} else {
				state = 0;
			}
			continue;

		case 5: /* Open bracket found */
			if (buffer[x] == '[')
				state = 6;
			continue;

		case 6:
			if (buffer[x] >= '0' && buffer[x] <= '9') {
				state = 7;
				start = &buffer[x];
			}
			continue;

		case 7: /* Store ID & Close bracket check */
			if (buffer[x] != ' ' && buffer[x] != ']')
				continue;

			/* End of a node ID */
			ret++;
			if (*ids)
				*ids = realloc(*ids,
					       sizeof(uint64_t) * ret);
			else
				*ids = malloc(sizeof(uint64_t) * ret);

			(*ids)[ret - 1] = atoi(start);
			start = NULL;
			state = 6;

			if (buffer[x] == ']')
				return ret;
			continue;

		default:
			printf("Invalid state: %d\n", state);
			return 0;
		}

		/* No match. */
		state = 0;
	}

	return 0;
}


static size_t
__read_services(char **buffer)
{
	int fd, n, ret = 0;

	*buffer = NULL;

	fd = open("/proc/cluster/services", O_RDONLY);
	*buffer = malloc(BLOCK_SIZE);

	while ((n = read(fd, *buffer, BLOCK_SIZE)) == BLOCK_SIZE) {
		ret += BLOCK_SIZE;
		*buffer = realloc(*buffer, ret + BLOCK_SIZE);
	}

	if (n < 0) {
		if (*buffer) {
			free(*buffer);
			*buffer = NULL;
		}
		return 0;
	}

	ret += n;
	return ret;
}


static int
__is_member(uint64_t *member_ids, int idlen, uint64_t nodeid)
{
	int x;

	for (x = 0; x < idlen; x++) {
		if (nodeid == member_ids[x])
			return 1;
	}

	return 0;
}


cluster_member_list_t *
service_group_members(int sockfd, char *groupname)
{
       	cluster_member_list_t *foo = NULL;
	struct cl_cluster_node *cman_list = NULL;
	int x, y, count = 0, group_count;
	size_t sz = 0;
	char *buf = NULL;
	uint64_t *member_ids = NULL;

	count = ioctl(sockfd, SIOCCLUSTER_GETMEMBERS, NULL);
	if (count <= 0)
		return NULL;
	
	/* BIG malloc here */
	cman_list = malloc(cml_size(count));
	assert(cman_list != NULL);
	
	/* Another biggie */
	foo = malloc(cml_size(count));
	assert(foo != NULL);
	assert(ioctl(sockfd, SIOCCLUSTER_GETMEMBERS, cman_list) == count);
	strncpy(foo->cml_groupname, groupname, sizeof(foo->cml_groupname));

	sz = __read_services(&buf);
	if (sz <= 0) {
		free(cman_list);
		free(foo);
		return NULL;
	}

	group_count = __group_member_ids(groupname, buf, sz, &member_ids);
	if (group_count <= 0) {
		free(cman_list);
		free(foo);
		return NULL;
	}

	foo->cml_count = group_count;
	for (x = 0, y = 0; (x < count) && (y < group_count); x++) {
		if (!__is_member(member_ids, group_count,
		    		 cman_list[x].node_id))
			continue;

		foo->cml_members[y].cm_id = cman_list[x].node_id;

		switch(cman_list[x].state) {
		case NODESTATE_REMOTEMEMBER:
		case NODESTATE_MEMBER:
			foo->cml_members[y].cm_state = STATE_UP;
			break;
		case NODESTATE_JOINING:
		case NODESTATE_DEAD:
			foo->cml_members[y].cm_state = STATE_DOWN;
			break;
		default:
			foo->cml_members[y].cm_state = STATE_INVALID;
			break;
		}
		
		strncpy(foo->cml_members[y].cm_name, cman_list[x].name,
			sizeof(foo->cml_members[y].cm_name));
		++y;
	}

	if (buf)
		free(buf);
	if (member_ids)
		free(member_ids);
	if (cman_list)
		free(cman_list);
	return foo;
}


/*
int
main(int argc, char **argv)
{
	uint64_t *member_id = NULL;
	uint32_t count, x;
	char *buf = NULL;
	size_t len = 0;

#if 0
	printf("***\n");
	printf(TEST);
	printf("***\n");
#endif
	len = __read_services(&buf);
	count = __group_member_ids(argv[1], buf, len, &member_id);

	printf("Count = %d\n", count);

	for (x = 0; x < count; x++) {
		printf("member_id[%d] = %ld\n", x, member_id[x]);
	}

	if (buf)
		free(buf);

	if (member_id)
		free(member_id);
}
*/

