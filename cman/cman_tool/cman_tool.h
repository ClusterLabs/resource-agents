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

#ifndef __CMAN_TOOL_DOT_H__
#define __CMAN_TOOL_DOT_H__

#include <sys/types.h>
#include <sys/uio.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/utsname.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <net/if.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <stddef.h>
#include <stdint.h>
#include <fcntl.h>
#include <netdb.h>
#include <limits.h>
#include <unistd.h>


extern char *prog_name;

#ifndef TRUE
#define TRUE 1
#define FALSE 0
#endif

#define die(fmt, args...) \
do { \
	fprintf(stderr, "%s: ", prog_name); \
	fprintf(stderr, fmt "\n", ##args); \
	exit(EXIT_FAILURE); \
} while (0)


#define DEFAULT_PORT 6809
#define DEFAULT_VOTES 1
#define MAX_INTERFACES 10
#define MAX_NODE_NAME_LEN 65
#define MAX_MCAST_NAME_LEN 256
#define MAX_PATH_LEN 256


struct commandline
{
	int operation;
	int num_multicasts;
        int num_interfaces;
        int num_nodenames;
	char *multicast_names[MAX_INTERFACES];
	char *nodenames[MAX_INTERFACES];
        char *interfaces[MAX_INTERFACES];
	int votes;
	int expected_votes;
	int two_node;
	int port;
	char clustername[MAX_CLUSTER_NAME_LEN];
	int no_ccs;
	int remove;
	int force;
        int verbose;
        int nodeid;
	unsigned int config_version;

	int config_version_opt;
	int votes_opt;
	int expected_votes_opt;
	int port_opt;
	int nodeid_opt;
	int clustername_opt;
	int wait_opt;
	int wait_quorate_opt;
};
typedef struct commandline commandline_t;

int join(commandline_t *comline);
int get_ccs_join_info(commandline_t *comline);

#endif  /*  __CMAN_TOOL_DOT_H__  */
