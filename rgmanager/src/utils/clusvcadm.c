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
#include <members.h>
#include <message.h>
#include <libcman.h>
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


int
do_lock_req(int req)
{
	cman_handle_t ch;
	msgctx_t ctx;
	int ret = RG_FAIL;
	cluster_member_list_t *membership = NULL;
	int me;
	generic_msg_hdr hdr;

	ch = cman_init(NULL);
	if (!ch) {
		printf("Could not connect to cluster service\n");
		goto out;
	}

	membership = get_member_list(ch);
	me = get_my_nodeid(ch);

	if (msg_open(0, RG_PORT, &ctx, 5) < 0) {
		printf("Could not connect to resource group manager\n");
		goto out;
	}

	if (msg_send_simple(&ctx, req, 0, 0) < 0) {
		printf("Communication failed\n");
		goto out;
	}

	if (msg_receive(&ctx, &hdr, sizeof(hdr), 5) < sizeof(hdr)) {
		printf("Receive failed\n");
		goto out;
	}

	swab_generic_msg_hdr(&hdr);
	ret = hdr.gh_command;

out:
	if (membership)
		free_member_list(membership);

	if (ctx.type >= 0)
		msg_close(&ctx);

	if (ch)
		cman_finish(ch);

	return ret;
}


int
do_lock(void)
{
	if (do_lock_req(RG_LOCK) != RG_SUCCESS) {
		printf("Lock operation failed\n");
		return 1;
	}
	printf("Resource groups locked\n");
	return 0;
}


int
do_unlock(void)
{
	if (do_lock_req(RG_UNLOCK) != RG_SUCCESS) {
		printf("Unlock operation failed\n");
		return 1;
	}
	printf("Resource groups unlocked\n");
	return 0;
}


int
do_query_lock(void)
{
	switch(do_lock_req(RG_QUERY_LOCK)) {
	case RG_LOCK:
		printf("Resource groups locked\n");
		break;
	case RG_UNLOCK:
		printf("Resource groups unlocked\n");
		break;
	default:
		printf("Query operation failed\n");
		return 1;
	}
	return 0;
}


void
usage(char *name)
{
printf("Resource Group Control Commands:\n");
printf("       %s -v                     Display version and exit\n",name);
printf("       %s -d <group>             Disable <group>\n", name);
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
printf("\n");
printf("Resource Group Locking (for cluster Shutdown / Debugging):\n");
printf("       %s -l                     Lock local resource group manager.\n"
       "                                 This prevents resource groups from\n"
       "                                 starting on the local node.\n",
       name);
printf("       %s -S                     Show lock state\n", name);
printf("       %s -u                     Unlock local resource group manager.\n"
       "                                 This allows resource groups to start\n"
       "                                 on the local node.\n", name);
}


int
main(int argc, char **argv)
{
	extern char *optarg;
	char *svcname=NULL, nodename[256];
	int opt;
	msgctx_t ctx;
	cman_handle_t ch;
	SmMessageSt msg;
	int action = RG_STATUS;
	int node_specified = 0;
       	int msgtarget, me, svctarget = 0;
	char *actionstr = NULL;
	cluster_member_list_t *membership;

	if (geteuid() != (uid_t) 0) {
		fprintf(stderr, "%s must be run as the root user.\n", argv[0]);
		return 1;
	}

	while ((opt = getopt(argc, argv, "lSue:d:r:n:m:vR:s:qh?")) != EOF) {
		switch (opt) {
		case 'l':
			return do_lock();

		case 'S':
			return do_query_lock();

		case 'u':
			return do_unlock();

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
			action = RG_STOP_USER;
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
		case 'h':
		case '?':
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
	ch = cman_init(NULL);
	if (!ch) {
		printf("Could not connect to cluster service\n");
		return 1;
	}

	membership = get_member_list(ch);
	me = get_my_nodeid(ch);

	if (node_specified) {
		msgtarget = memb_name_to_id(membership, nodename);
		if (msgtarget == 0) {
			fprintf(stderr, "Member %s not in membership list\n",
				nodename);
			return 1;
		}
		svctarget = msgtarget;
	} else {
		msgtarget = me;
		/*
		clu_local_nodename(RG_SERVICE_GROUP, nodename,
				   sizeof(nodename));
				   */
	}
	
	build_message(&msg, action, svcname, svctarget);

	if (action != RG_RELOCATE) {
		printf("Member %s %s %s", nodename, actionstr, svcname);
		printf("...");
		fflush(stdout);
		msg_open(0, RG_PORT, &ctx, 5);
	} else {
		printf("Trying to relocate %s to %s", svcname, nodename);
		printf("...");
		fflush(stdout);
		msg_open(0, RG_PORT, &ctx, 5);
	}

	if (ctx.type < 0) {
		fprintf(stderr,
			"Could not connect to resource group manager!\n");
		return 1;
	}

	if (msg_send(&ctx, &msg, sizeof(msg)) != sizeof(msg)) {
		perror("msg_send");
		fprintf(stderr, "Could not send entire message!\n");
		return 1;
	}

	if (msg_receive(&ctx, &msg, sizeof(msg), 0) != sizeof(msg)) {
		perror("msg_receive");
		fprintf(stderr, "Error receiving reply!\n");
		return 1;
	}

	/* Decode */
	swab_SmMessageSt(&msg);
	switch (msg.sm_data.d_ret) {
	case SUCCESS:
		printf("success\n");
		break;
	case RG_EFAIL:
		printf("failed\n");
		break;
	case RG_EABORT:
		printf("cancelled by resource manager\n");
		break;
	case RG_ENOSERVICE:
		printf("failed: Service does not exist\n");
		break;
	case RG_EDEADLCK:
		printf("failed: Operation would deadlock\n");
		break;
	case RG_EAGAIN:
		printf("failed: Try again (resource groups locked)\n");
		break;
	default:
		printf("failed: unknown reason %d\n", msg.sm_data.d_ret);
		break;
	}

	return msg.sm_data.d_ret;
}
