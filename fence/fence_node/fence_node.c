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

int dispatch_fence_agent(char *victim);

int main(int argc, char *argv[])
{
	int error;

	if (argc != 2) {
		fprintf(stderr, "%s [-V] <node_name>\n", argv[0]);
		exit(EXIT_FAILURE);
	}

	if (!strcmp("-V", argv[1])) {
		printf("%s %s (built %s %s)\n", argv[0], FENCE_RELEASE_NAME,
		       __DATE__, __TIME__);
		printf("%s\n", REDHAT_COPYRIGHT);
		exit(EXIT_SUCCESS);
	}

	openlog("fence_node", LOG_PID, LOG_USER);

	error = dispatch_fence_agent(argv[1]);
	if (error) {
		syslog(LOG_ERR, "Fence of \"%s\" was unsuccessful\n", argv[1]);
		exit(EXIT_FAILURE);
	}

	syslog(LOG_NOTICE, "Fence of \"%s\" was successful\n", argv[1]);
	exit(EXIT_SUCCESS);
}
