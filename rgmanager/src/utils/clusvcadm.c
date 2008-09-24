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
build_message(SmMessageSt *msgp, int action, char *svcName, int target,
		int arg1, int arg2)
{
	msgp->sm_hdr.gh_magic = GENERIC_HDR_MAGIC;
	msgp->sm_hdr.gh_command = RG_ACTION_REQUEST;
	msgp->sm_hdr.gh_length = sizeof(*msgp);
	msgp->sm_hdr.gh_arg1 = arg1;
	msgp->sm_hdr.gh_arg2 = arg2;
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
printf("usage: %s [command]\n\n", name);

printf("Resource Group Control Commands:\n");
printf("  -v                     Display version and exit\n");
printf("  -d <group>             Disable <group>.  This stops a group\n"
       "                         until an administrator enables it again,\n"
       "                         the cluster loses and regains quorum, or\n"
       "                         an administrator-defined event script\n"
       "                         explicitly enables it again.\n");
printf("  -e <group>             Enable <group>\n");
printf("  -e <group> -F          Enable <group> according to failover\n"
       "                         domain rules (deprecated; always the\n"
       "                         case when using central processing)\n");
printf("  -e <group> -m <member> Enable <group> on <member>\n");
printf("  -r <group> -m <member> Relocate <group> [to <member>]\n"
       "                         Stops a group and starts it on another"
       "                         cluster member.\n");
printf("  -M <group> -m <member> Migrate <group> to <member>\n");
printf("                         (e.g. for live migration of VMs)\n");
printf("  -q                     Quiet operation\n");
printf("  -R <group>             Restart a group in place.\n");
printf("  -s <group>             Stop <group>.  This temporarily stops\n"
       "                         a group.  After the next group or\n"
       "                         or cluster member transition, the group\n"
       "                         will be restarted (if possible).\n");
printf("Resource Group Locking (for cluster Shutdown / Debugging):\n");
printf("  -l                     Lock local resource group managers.\n"
       "                         This prevents resource groups from\n"
       "                         starting.\n");
printf("  -S                     Show lock state\n");
printf("  -u                     Unlock resource group managers.\n"
       "                         This allows resource groups to start.\n");
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
	int fod = 0;
	int node_specified = 0;
       	int me, svctarget = 0;
	char *actionstr = NULL;
	cluster_member_list_t *membership;

	if (geteuid() != (uid_t) 0) {
		fprintf(stderr, "%s must be run as the root user.\n", argv[0]);
		return 1;
	}

	while ((opt = getopt(argc, argv, "lSue:M:d:r:n:m:FvR:s:Z:U:qh?")) != EOF) {
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
               case 'F':
                       if (node_specified) {
                               fprintf(stderr,
                                       "Cannot use '-F' with '-n' or '-m'\n");
                               return 1;
                       }
                       fod = 1;
                       break;
		case 'd':
			/* DISABLE */
			actionstr = "disabling";
			action = RG_DISABLE;
			svcname = optarg;
			break;
		case 'r':
			/* RELOCATE */
			actionstr = "relocate";
			action = RG_RELOCATE;
			svcname = optarg;
			break;
		case 'M':
			/* MIGRATE */
			actionstr = "migrate";
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
                       if (fod) {
                               fprintf(stderr,
                                       "Cannot use '-F' with '-n' or '-m'\n");
                               return 1;
                       }
			strncpy(nodename,optarg,sizeof(nodename));
			node_specified = 1;
			break;
		case 'v':
			printf("%s\n", RELEASE_VERSION);
			return 0;
		case 'Z':
			actionstr = "freezing";
			action = RG_FREEZE;
			svcname = optarg;
			break;
		case 'U':
			actionstr = "unfreezing";
			action = RG_UNFREEZE;
			svcname = optarg;
			break;
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

	if (action == RG_MIGRATE && !node_specified) {
		printf("Migration requires a target cluster member\n");
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
		//strcpy(nodename,"me");
	}

	build_message(&msg, action, svcname, svctarget, fod, 0);

	if (action != RG_RELOCATE && action != RG_MIGRATE) {
		if (!node_specified)
			printf("Local machine %s %s", actionstr, svcname);
		else
			printf("Member %s %s %s", nodename, actionstr, svcname);
		printf("...");
		fflush(stdout);
		if (msg_open(MSG_SOCKET, 0, RG_PORT, &ctx, 5) < 0) {
			printf("Could not connect to resource group manager\n");
			return 1;
		}
	} else {
		if (!svctarget)
			printf("Trying to %s %s", actionstr, svcname);
		else 
			printf("Trying to %s %s to %s", actionstr, svcname,
			       nodename);
		printf("...");
		fflush(stdout);
		if (msg_open(MSG_SOCKET, 0, RG_PORT, &ctx, 5) < 0) {
			printf("Could not connect to resource group manager\n");
			return 1;
		}
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
	if (msg.sm_data.d_ret == RG_ERUN)
		return 0;
	if (msg.sm_data.d_ret)
		return msg.sm_data.d_ret;
	
	switch (action) {
	/*case RG_MIGRATE:*/
	case RG_RELOCATE:
	case RG_START:
	case RG_ENABLE:
		printf("%s%s is now running on %s\n",
		       (!node_specified ||
		       msg.sm_data.d_svcOwner==svctarget)?"":"Warning: ",
		       svcname, memb_id_to_name(membership,
		       			        msg.sm_data.d_svcOwner));
		break;
	default:
		break;
	}
	
	return msg.sm_data.d_ret;
}
