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
 * Cluster Plugin Test Program.
 *
 * XXX Needs Doxygenification.
 */
#include <magma.h>
#include <dlfcn.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>




void
wait_for_events(int fd, char *groupname)
{
	cluster_member_list_t *membership;
	cluster_member_list_t *new, *lost, *gained;
	fd_set rfds;

	membership = clu_member_list(groupname);

	print_member_list(membership, 1);

	printf("=== Waiting for events.\n");

	while(1) {
		FD_ZERO(&rfds);
		FD_SET(fd, &rfds);

		select(fd+1, &rfds, NULL, NULL, NULL);
		
		switch(clu_get_event(fd)) {
		case CE_NULL:
			printf("-E- Spurious wakeup\n");
			break;
		case CE_SUSPEND:
			printf("*E* Suspend activities\n");
			break;
		case CE_MEMB_CHANGE:
			printf("*E* Membership change\n");

			new = clu_member_list(groupname);
			lost = memb_lost(membership, new);
			gained = memb_gained(membership, new);

			if (lost) {
				printf("<<< Begin Nodes lost\n");
				print_member_list(lost, 0);
				printf("<<< End Nodes Lost\n");
				free(lost);
			}

			if (gained) {
				printf(">>> Begin Nodes gained\n");
				print_member_list(gained, 0);
				printf(">>> End Nodes gained\n");
				free(gained);
			}

			free(membership);
			membership = new;

			break;
		case CE_QUORATE:
			printf("*E* Quorum formed\n");
			break;
		case CE_INQUORATE:
			printf("*E* Quorum dissolved\n");
			break;
		case CE_SHUTDOWN:
			printf("*E* Node shutdown\n");
			exit(0);
		}
	}
}


void
lock_something(void)
{
	void *lockp = NULL;
	int ret;

	printf("Locking \"Something\"...");
	fflush(stdout);

	ret = clu_lock("Something", CLK_EX, &lockp);

	if (ret != 0) {
		printf("FAILED: %d\n", ret);
		return;
	}

	printf("OK\n");
	printf("Press <ENTER> to unlock\n");
	getchar();

	printf("Unlocking \"Something\"...");
	fflush(stdout);
	if (clu_unlock("Something", lockp) != 0) {
		printf("FAILED\n");
		return;
	}
	printf("OK\n");
}


void
perform_op(int fd, char *op, char *data)
{
	cluster_member_list_t *nodelist;
	int x;
	char *state;
	char name[256];
	uint64_t nid;

	if (strcmp(op, "null") == 0) {
		clu_null();
	} else if (strcmp(op, "localname") == 0) {
		if (clu_local_nodename("cluster::usrm", name, 256) == 0) {
			printf("Local node name = %s\n", name);
		}
	} else if (strcmp(op, "localid") == 0) {
		if (clu_local_nodeid("cluster::usrm", &nid) == 0) {
			printf("Local node id = 0x%x\n", (uint32_t)nid);
		}
	} else if (strcmp(op, "lock") == 0) {
		lock_something();
	} else if (strcmp(op, "members") == 0) {
		nodelist = clu_member_list(data);
		if (nodelist) {
			for (x=0; x<nodelist->cml_count; x++) {

				switch (nodelist->cml_members[x].cm_state) {
				case STATE_DOWN:
					state = "DOWN";
					break;
				case STATE_UP:
					state = "UP";
					break;
				default:
					state = "UNKNOWN";
					break;
				}
				printf("Member ID 0x%016llx: %s, state %s\n",
				       (unsigned long long)
					nodelist->cml_members[x].cm_id,
				       nodelist->cml_members[x].cm_name,
				       state);
			}
		}
	} else if (strcmp(op, "quorum") == 0) {
		switch (clu_quorum_status(data)) {
		case (QF_QUORATE|QF_GROUPMEMBER):
			state = "Quorate & Group Member";
			break;
		case (QF_QUORATE):
			state = "Quorate";
			break;
		default:
			state = "Inquorate";
			break;
		}
		printf("Quorum state = %s\n", state);
	} else if (strcmp(op, "listen") == 0) {
		printf("Listening for events (group %s)...\n", data?data:"cluster::usrm");
		wait_for_events(fd, data?data:"cluster::usrm");
	}
}


int
main(int argc, char **argv)
{
	int fd, login=1;

	if (argc < 2) {
		printf("usage: %s [<op> <data>]\n",
		       argv[0]);
		return 1;
	}

	if (!strcmp(argv[1], "nologin")) {
		login = 0;
		--argc;
		++argv;
	}

	fd = clu_connect("cluster::usrm", login);
	
	if (fd < 0) {
		printf("Connect failure: %s\n", strerror(-fd));
		return -1;
	}

	printf("Connected via: %s\n",clu_plugin_version());

	if (argc == 2) {
		perform_op(fd, argv[1], NULL);
	} else if (argc == 3) {
		perform_op(fd, argv[1], argv[2]);
	}

	clu_disconnect(fd);

	return 0;
}
