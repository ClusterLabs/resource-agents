#include <magma.h>
#include <magmamsg.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>

#define MY_BASE_PORT 13412
#define MY_SERVICE_GROUP "magma::ping"
#define MY_PURPOSE 0x12389af

int quorate = 0;
cluster_member_list_t *membership = NULL;

int
handle_cluster_event(int fd)
{
	cluster_member_list_t *new, *gained, *lost;
	int ret;
	
	ret = clu_get_event(fd);

	switch(ret) {
	case CE_NULL:
		//printf("-E- Spurious wakeup\n");
		break;
	case CE_SUSPEND:
		printf("*E* Suspend activities\n");
		break;
	case CE_MEMB_CHANGE:
		printf("*E* Membership change\n");
		
		new = clu_member_list(MY_SERVICE_GROUP);
		lost = memb_lost(membership, new);
		gained = memb_gained(membership, new);

		if (lost) {
			printf("<<< Begin Nodes lost\n");
			print_member_list(lost, 0);
			printf("<<< End Nodes Lost\n");
			cml_free(lost);
		}

		if (gained) {
			printf(">>> Begin Nodes gained\n");
			print_member_list(gained, 0);
			printf(">>> End Nodes gained\n");
			cml_free(gained);
		}

		memb_resolve_list(new, membership);
		msg_update(new);
		cml_free(membership);
		membership = new;
		break;

	case CE_QUORATE:
		quorate = 1;
		printf("*E* Quorum formed\n");
		break;
	case CE_INQUORATE:
		quorate = 0;
		printf("*E* Quorum dissolved\n");
		break;
	case CE_SHUTDOWN:
		printf("*E* Node shutdown\n");
		exit(0);
	}

	return ret;
}


uint64_t
next_node_id(uint64_t me)
{
	uint64_t low = (uint64_t)(-1);
	uint64_t next = me, curr;
	int x;

	for (x = 0; x < membership->cml_count; x++) {
		curr = membership->cml_members[x].cm_id;
		if (curr < low)
			low = curr;

		if ((curr > me) && ((next == me) || (curr < next)))
			next = curr;
	}

	/* I am highest ID; go to lowest */
	if (next == me)
		next = low;

	return next;
}


uint32_t
send_to_next(uint32_t msg)
{
	int fd = -1;
	uint32_t mymsg;
	uint64_t my_node_id = (uint64_t)-1;
	uint64_t target;

	if (my_node_id == (uint64_t)-1)
		clu_local_nodeid(MY_SERVICE_GROUP, &my_node_id);

	target = my_node_id;

	do {
		target = next_node_id(target);
		if (target == my_node_id) {
			printf("I am all alone :(\n");
			return msg;
		}
		fd = msg_open(target, MY_BASE_PORT, MY_PURPOSE, 5);
		if (fd != -1)
			break;
	} while (1);

	/* Bump pinger. */
	mymsg = msg + 1;

	printf("Sending %d to %s\n", mymsg,
	       memb_id_to_name(membership, target));
	msg_send(fd, &mymsg, sizeof(mymsg));
	msg_close(fd);
	return mymsg;
}


uint32_t 
ping_pong_message(int fd, uint32_t mycount, uint64_t nodeid)
{
	int n;
	uint32_t msg;

	n = msg_receive_timeout(fd, &msg, sizeof(msg), 5);

	if (n == -1) {
		perror("msg_receive_timeout");
		return mycount;
	}

	printf("Received %d from %s\n", msg,
	       memb_id_to_name(membership,nodeid));

	if (msg <= mycount)
		msg = mycount;

	sleep(1);

	return send_to_next(msg);
}


int
wait_for_stuff_to_happen(int clusterfd, int *listen_fds, int lfd_count)
{
	int newfd, fd, n, max;
	fd_set rfds;
	static uint32_t pingpong = 1;
	struct timeval tv;
	uint64_t nodeid;

	FD_ZERO(&rfds);
	max = msg_fill_fdset(&rfds, MSG_ALL, MSGP_ALL);

	tv.tv_sec = 5;
	tv.tv_usec = 0;
	n = select(max + 1, &rfds, NULL, NULL, &tv);

	/* No new messages.  See if someone else logged in to the group */
	if (n == 0) {
		printf("Looking for my friends...\n");
		pingpong = send_to_next(pingpong);
		return 0;
	}

	if (n < 0) {
		printf("Select error: %s\n", strerror(errno));
		sleep(1);
		return 0;
	}

	while (n) {
		fd = msg_next_fd(&rfds);
		if (fd == -1)
			break;

		--n;

		if (fd == clusterfd) {
			if (handle_cluster_event(clusterfd) ==
				CE_MEMB_CHANGE) {
				pingpong = send_to_next(pingpong);
			}

			continue;
		}

		/* One of our listen file descriptors */
		newfd = msg_accept(fd, 1, &nodeid);
		if (quorate) {
			pingpong = ping_pong_message(newfd, pingpong,
						     nodeid);
		} else {
			printf("Dropping connect from %s: NO QUORUM\n",
			       memb_id_to_name(membership, nodeid));
		}
		msg_close(newfd);
	}

	return 0;
}


int
main(void)
{
	int cluster_fd;
	int listen_fds[2], listeners;

	if ((listeners = msg_listen(MY_BASE_PORT, MY_PURPOSE,
				    listen_fds, 2)) <= 0) {
		printf("Couldn't set up listen socket\n");
		return -1;
	}

	/* Connect to the cluster software. */
	cluster_fd = clu_connect(MY_SERVICE_GROUP, 1);

	if (cluster_fd < 0) {
		switch(errno) {
		case ELIBACC:
			fprintf(stderr, "No plugins found.\n");
			break;
		case ESRCH:
			fprintf(stderr, "No matching plugin found or cluster "
				"not running.\n");
			break;
		default:
			fprintf(stderr, "%s\n", strerror(errno));
			break;
		}

		return -1;
	}

	quorate = (clu_quorum_status(MY_SERVICE_GROUP) & QF_QUORATE);
	printf("Using plugin: %s\n", clu_plugin_version());
	printf("Initial status = %s\n", quorate?"Quorate":"Inquorate");

	membership = clu_member_list(MY_SERVICE_GROUP);
	msg_update(membership);
	memb_resolve_list(membership, NULL);
	print_member_list(membership, 1);

	while (1) {
		wait_for_stuff_to_happen(cluster_fd, listen_fds, listeners);
	}
}

