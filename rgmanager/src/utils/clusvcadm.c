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
 * The New And Improved Cluster Service Admin Utility.
 * TODO Clean up the code.
 */
#include <stdio.h>
#include <stdlib.h>
#include <sys/param.h>
#include <libgen.h>
#include <resgroup.h>
#include <platform.h>
#include <magma.h>
#include <magmamsg.h>
#include <resgroup.h>
#include <msgsimple.h>

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif


void
build_message(SmMessageSt *msgp, int action, char *svcName, uint64_t target)
{
	msgp->sm_hdr.gh_magic = GENERIC_HDR_MAGIC;
	msgp->sm_hdr.gh_command = RG_ACTION_REQUEST;
	msgp->sm_hdr.gh_length = sizeof(*msgp);
	msgp->sm_data.d_action = action;
	strncpy(msgp->sm_data.d_svcName, svcName,
		sizeof(msgp->sm_data.d_svcName));
	msgp->sm_data.d_svcOwner = target;
	msgp->sm_data.d_ret = 0;

	swab_SmMessageSt(msgp);
}



void
usage(char *name)
{
printf("usage: %s -d <group>             Disable <group>\n", name);
printf("       %s -e <group>             Enable <group>\n",
       name);
printf("       %s -e <group> -m <member> Enable <group>"
       " on <member>\n", name);
printf("       %s -r <group> -m <member> Relocate <group> [to <member>]\n",
	       name);
printf("       %s -q                     Quiet operation\n", name);
printf("       %s -R <group>             Restart a group in place.\n",
       name);
printf("       %s -s <group>             Stop <group>\n", name);
printf("       %s -v                     Display version and exit\n",name);
}


int
main(int argc, char **argv)
{
	extern char *optarg;
	char *svcname=NULL, nodename[256];
	int opt;
	int msgfd = -1, fd;
	SmMessageSt msg;
	int action = RG_STATUS, svctarget = -1;
	int node_specified = 0;
       	uint64_t msgtarget;
	char *actionstr = NULL;
	cluster_member_list_t *membership;

	if (geteuid() != (uid_t) 0) {
		fprintf(stderr, "%s must be run as the root user.\n", argv[0]);
		return 1;
	}

	while ((opt = getopt(argc, argv, "e:d:r:n:m:vR:s:S:q")) != EOF) {
		switch (opt) {
		case 'e':
			/* ENABLE */
			actionstr = "trying to enable";
			action = RG_ENABLE;
			svcname = optarg;
			break;
		case 'd':
			/* DISABLE */
			actionstr = "disabling";
			action = RG_DISABLE;
			svcname = optarg;
			break;
		case 'r':
			/* RELOCATE */
			actionstr = "trying to relocate";
			action = RG_RELOCATE;
			svcname = optarg;
			break;
		case 's':
			/* stop */
			actionstr = "stopping";
			action = RG_STOP;
			svcname = optarg;
			break;
		case 'R':
			actionstr = "trying to restart";
			action = RG_RESTART;
			svcname = optarg;
			break;
		case 'm': /* member ... */
		case 'n': /* node .. same thing */

			strncpy(nodename,optarg,sizeof(nodename));
			node_specified = 1;
			break;
		case 'v':
			printf("%s\n",PACKAGE_VERSION);
			return 0;
		case 'q':
			close(STDOUT_FILENO);
			break;
		default:
			usage(basename(argv[0]));
			return 1;
		}
	}

	if (!svcname) {
		usage(basename(argv[0]));
		return 1;
	}
	

	/* No login */
	fd = clu_connect(RG_SERVICE_GROUP, 0);
	if (fd < 0) {
		printf("Could not connect to cluster service\n");
		return 1;
	}

	membership = clu_member_list(RG_SERVICE_GROUP);
	msg_update(membership);

	if (node_specified) {
		msgtarget = memb_name_to_id(membership, nodename);
		if (msgtarget == NODE_ID_NONE) {
			fprintf(stderr, "Member %s not in membership list\n",
				nodename);
			return 1;
		}
	} else {
		clu_local_nodeid(RG_SERVICE_GROUP, &msgtarget);
		clu_local_nodename(RG_SERVICE_GROUP, nodename,
				   sizeof(nodename));
	}
	

	printf("Member %s %s %s", nodename, actionstr, svcname);
	printf("...");
	fflush(stdout);

	build_message(&msg, action, svcname, svctarget);

	msgfd = msg_open(msgtarget, RG_PORT, 0, 5);
	if (msgfd < 0) {
		fprintf(stderr,
			"Could not connect to resource group manager!\n");
		return 1;
	}

	if (msg_send(msgfd, &msg, sizeof(msg)) != sizeof(msg)) {
		perror("msg_send");
		fprintf(stderr, "Could not send entire message!\n");
		return 1;
	}

	if (msg_receive(msgfd, &msg, sizeof(msg)) != sizeof(msg)) {
		perror("msg_receive");
		fprintf(stderr, "Error receiving reply!\n");
		return 1;
	}

	/* Decode */
	swab_SmMessageSt(&msg);
	if (msg.sm_data.d_ret == SUCCESS) {
		printf("success\n");
	} else if (msg.sm_data.d_ret == ABORT) {
		printf("cancelled by resource manager\n");
	} else
		printf("failed\n");


	return msg.sm_data.d_ret;
}
