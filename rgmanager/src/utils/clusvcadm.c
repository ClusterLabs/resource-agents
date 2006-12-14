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
#include <signal.h>

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif


void
build_message(SmMessageSt *msgp, int action, char *svcName, int target)
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

	if (msg_open(MSG_SOCKET, 0, RG_PORT, &ctx, 5) < 0) {
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
printf("       %s -M <group> -m <member> Migrate <group> [to <member>]\n",
	       name);
printf("                                 (e.g. for live migration of Xen VMs)\n");
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
find_closest_node(cluster_member_list_t *cml, char *name, size_t maxlen)
{
	int x, c = 0, cl = 0, nc = 0, ncl = 0, cur = 0;

	for (x=0; x<cml->cml_count; x++) {
		cur = 0;

		while (cml->cml_members[x].cn_name[cur] && name[cur] &&
		       (cml->cml_members[x].cn_name[cur] == name[cur]))
			cur++;
		if (!cur)
			continue;
		if (cur >= cl) {
			ncl = cl; /* Next-closest */
			nc = c;
			cl = cur;
			c = x;
		}
	}

	if (!cl) {
		printf("No matches for '%s' found\n", name);
		return 0;
	}

	if (ncl == cl) {
		printf("More than one possible match for '%s' found\n",
		       name);
		return 0;
	}

	printf("Closest match: '%s'\n", 
	       cml->cml_members[c].cn_name);

	strncpy(name, cml->cml_members[c].cn_name, maxlen);
	return cml->cml_members[c].cn_nodeid;
}


int
main(int argc, char **argv)
{
	extern char *optarg;
	char *svcname=NULL, nodename[256], realsvcname[64];
	int opt;
	msgctx_t ctx;
	cman_handle_t ch;
	SmMessageSt msg;
	generic_msg_hdr *h = (generic_msg_hdr *)&msg;
	int action = RG_STATUS;
	int node_specified = 0;
       	int me, svctarget = 0;
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
		case 'M':
			/* MIGRATE */
			actionstr = "trying to migrate";
			action = RG_MIGRATE;
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

	if (!strchr(svcname,':')) {
		snprintf(realsvcname, sizeof(realsvcname), "service:%s",
			 svcname);
		svcname = realsvcname;
	}

	signal(SIGPIPE, SIG_IGN);

	/* No login */
	ch = cman_init(NULL);
	if (!ch) {
		printf("Could not connect to cluster service\n");
		return 1;
	}

	membership = get_member_list(ch);
	me = get_my_nodeid(ch);

	if (node_specified) {
		svctarget = memb_name_to_id(membership, nodename);
		if (svctarget == 0) {
			printf("'%s' not in membership list\n",
			       nodename);
			svctarget = find_closest_node(membership, nodename,
						      sizeof(nodename));
			if (!svctarget)
				return 1;
		}
	} else {
		svctarget = 0;
		/*
		clu_local_nodename(RG_SERVICE_GROUP, nodename,
				   sizeof(nodename));
				   */
		strcpy(nodename,"me");
	}
	
	build_message(&msg, action, svcname, svctarget);

	if (action != RG_RELOCATE && action != RG_MIGRATE) {
		printf("Member %s %s %s", nodename, actionstr, svcname);
		printf("...");
		fflush(stdout);
		msg_open(MSG_SOCKET, 0, RG_PORT, &ctx, 5);
	} else {
		if (!svctarget)
			printf("Trying to relocate %s", svcname);
		else 
			printf("Trying to relocate %s to %s", svcname,
			       nodename);
		printf("...");
		fflush(stdout);
		msg_open(MSG_SOCKET, 0, RG_PORT, &ctx, 5);
	}

	if (ctx.type < 0) {
		fprintf(stderr,
			"Could not connect to resource group manager!\n");
		return 1;
	}

	msg_send(&ctx, &msg, sizeof(msg));

	/* Reusing opt here */
	if ((opt = msg_receive(&ctx, &msg, sizeof(msg), 0)) < sizeof(*h)) {
		perror("msg_receive");
		fprintf(stderr, "Error receiving reply!\n");
		return 1;
	}

	/* Decode */
	if (opt < sizeof(msg)) {
		swab_generic_msg_hdr(h);
		printf("%s\n", rg_strerror(h->gh_arg1));
		return h->gh_arg1;
	}

	swab_SmMessageSt(&msg);
	printf("%s\n", rg_strerror(msg.sm_data.d_ret));
	switch (action) {
	case RG_MIGRATE:
	case RG_RELOCATE:
	case RG_START:
	case RG_ENABLE:
		printf("%s%s is now running on %s\n",
		       msg.sm_data.d_svcOwner==svctarget?"":"Warning: ",
		       svcname, memb_id_to_name(membership,
		       			        msg.sm_data.d_svcOwner));
		break;
	default:
		break;
	}
	
	return msg.sm_data.d_ret;
}
