/******************************************************************************
*******************************************************************************
**
**  Copyright (C) Sistina Software, Inc.  1997-2003  All rights reserved.
**  Copyright (C) 2004 Red Hat, Inc.  All rights reserved.
**  
**  This copyrighted material is made available to anyone wishing to use,
**  modify, copy, or redistribute it subject to the terms and conditions
**  of the GNU General Public License v.2.
**
*******************************************************************************
******************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <syslog.h>

#include "copyright.cf"
#include "ccs.h"

#define OPTION_STRING           ("c:hOuV")

#define die(fmt, args...) \
do \
{ \
  fprintf(stderr, "%s: ", prog_name); \
  fprintf(stderr, fmt "\n", ##args); \
  exit(EXIT_FAILURE); \
} \
while (0)

char *prog_name;
int unfence;

int dispatch_fence_agent(char *victim, int in);
int dispatch_fence_agent_force(char *victim, char *cluster, int in);

static void print_usage(void)
{
	printf("Usage:\n");
	printf("\n");
	printf("%s [options] node_name\n", prog_name);
	printf("\n");
	printf("Options:\n");
	printf("\n");
	printf("  -c <cluster>     Specify the cluster name\n");
	printf("  -h               Print this help, then exit\n");
	printf("  -O               Override quorum requirement\n");
	printf("  -u               Unfence the node\n");
	printf("  -V               Print program version information, then exit\n");
	printf("\n");
}

int main(int argc, char *argv[])
{
	int cont = 1, optchar, error;
	int ccs_desc;
	int force=0;
	char *victim = NULL;
	char *cluster_name = NULL;
	char *current_cluster_name = NULL;

	prog_name = argv[0];

	while (cont) {
		optchar = getopt(argc, argv, OPTION_STRING);

		switch (optchar) {

		case 'c':
			cluster_name = optarg;
			break;

		case 'h':
			print_usage();
			exit(EXIT_SUCCESS);
			break;

		case 'O':
			force = 1;
			break;

		case 'u':
			unfence = 1;
			break;

		case 'V':
			printf("%s %s (built %s %s)\n", prog_name,
				FENCE_RELEASE_NAME, __DATE__, __TIME__);
			printf("%s\n", REDHAT_COPYRIGHT);
			exit(EXIT_SUCCESS);
			break;

		case ':':
		case '?':
			fprintf(stderr, "Please use '-h' for usage.\n");
			exit(EXIT_FAILURE);
			break;

		case EOF:
			cont = 0;
			break;

		default:
			die("unknown option: %c", optchar);
			break;
		};
	}

	while (optind < argc) {
		if (victim)
			die("unknown option %s", argv[optind]);
		victim = argv[optind];
		optind++;
	}

	if (!victim)
		die("no node name specified");

	if(force && !cluster_name){
		die("The '-O' option requires the '-c <cluster_name>' option.\n");
	}

	if(cluster_name){
		/* Check that CCS contains this cluster */
		ccs_desc = ccs_connect();
		if(ccs_desc < 0){
			if(force){
				ccs_desc = ccs_force_connect(cluster_name, 0);
				if(ccs_desc < 0){
					ccs_desc = ccs_force_connect(NULL, 0);
				}
				if(ccs_desc < 0){
					die("Unable to connect to CCS.\n"
					    "Hint: Is the daemon running?\n");
				}
			} else {
				die("Unable to connect to CCS.\n"
				    "Hint: If the cluster is not quorate, try using '-O'\n");
			}
		}
		if(ccs_get(ccs_desc, "/cluster/@name", &current_cluster_name)){
			ccs_disconnect(ccs_desc);
			die("Unable to get the current cluster name from CCS.\n");
		}
		if(strcmp(current_cluster_name, cluster_name)){
			ccs_disconnect(ccs_desc);
			die("Cluster names differ, refusing fence request.\n"
			    "CCS cluster name      : %s\n"
			    "Specified cluster name: %s\n",
			    current_cluster_name, cluster_name);
		}
		ccs_disconnect(ccs_desc);
	} else {
		ccs_desc = ccs_connect();	
		if(ccs_desc < 0){
			die("Unable to connect to CCS.\n"
			    "Hint: If the cluster is not quorate, try using '-O'\n");
		}
		ccs_disconnect(ccs_desc);
	}

	if (unfence) {
		dispatch_fence_agent_force(victim, cluster_name, 1);
		exit(EXIT_SUCCESS);
	}

	openlog("fence_node", LOG_PID, LOG_USER);

	error = dispatch_fence_agent_force(victim, cluster_name, 0);
	if (error) {
		syslog(LOG_ERR, "Fence of \"%s\" was unsuccessful\n", argv[1]);
		exit(EXIT_FAILURE);
	}

	syslog(LOG_NOTICE, "Fence of \"%s\" was successful\n", argv[1]);
	exit(EXIT_SUCCESS);
}

/*
 * Overrides for Emacs so that we follow Linus's tabbing style.
 * Emacs will notice this stuff at the end of the file and automatically
 * adjust the settings for this buffer only.  This must remain at the end
 * of the file.
 * ---------------------------------------------------------------------------
 * Local variables:
 * c-file-style: "linux"
 * End:
 */
