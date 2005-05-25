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

#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include <ctype.h>
#include <stdint.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <syslog.h>

#include "copyright.cf"
#include "libcman.h"

/* FIFO_DIR needs to agree with the same in manual/ack.c */

#define OPTION_STRING                   "hn:s:p:qV"
#define LOCK_DIR			"/var/lock"
#define FIFO_DIR			"/tmp"

#define MAX_NODES			256

char path[256];
char lockdir[256];
char fifodir[256];
char fname[256];

char args[256];
char agent[256];
char victim[256];

cman_node_t nodes[MAX_NODES];

char *prog_name;

int quiet_flag;
int fifo_fd;


void print_usage(void)
{
	printf("Usage:\n");
	printf("\n");
	printf("%s [options]\n", prog_name);
	printf("\n");
	printf("Options:\n");
	printf("  -h               usage\n");
	printf("  -q               quiet\n");
	printf("  -n <nodename>    node to fence\n");
	printf("  -V               version\n");
}

void get_options(int argc, char **argv)
{
	int c, rv;
	char *curr;
	char *rest;
	char *value;

	if (argc > 1) {
		while ((c = getopt(argc, argv, OPTION_STRING)) != -1) {
			switch(c) {
			case 'h':
				print_usage();
				exit(0);

			case 'n':
				if (strlen(optarg) > 255) {
					fprintf(stderr, "node name too long\n");
					exit(1);
				}
				strcpy(victim, optarg);
				break;

			case 'q':
				quiet_flag = 1;
				break;

			case 'p':
				if (strlen(optarg) > 200) {
					fprintf(stderr, "path name too long\n");
					exit(1);
				}
				strncpy(path, optarg, 200);
				break;

			case 'V':
				printf("%s %s (built %s %s)\n", prog_name,
					FENCE_RELEASE_NAME,
					__DATE__, __TIME__);
				printf("%s\n", REDHAT_COPYRIGHT);
				exit(0);
				break;
	
			case ':':		
			case '?':
				fprintf(stderr, "Please use '-h' for usage.\n");
				exit(1);
				break;

			default:
				fprintf(stderr, "unknown option: %c\n", c);
				exit(1);
				break;

			}
		}
	} else {
		if ((rv = read(0, args, 255)) < 0) {
			if (!quiet_flag)
				printf("failed: no input\n");
			exit(1);
		}

		curr = args;

		while ((rest = strchr(curr, '\n')) != NULL) {
			*rest = 0;
			rest++;
			if ((value = strchr(curr, '=')) == NULL) {
				printf("failed: invalid input\n");
				exit(1);
			}
			*value = 0;
			value++;
			if (!strcmp(curr, "agent")){
				strcpy(agent, value);
				prog_name = agent;
			}
			if (!strcmp(curr, "nodename"))
				strcpy(victim, value);
			/* deprecated */
			if (!strcmp(curr, "ipaddr"))
				strcpy(victim, value);
			curr = rest;
		}
	}

	if (!strlen(path))
		strcpy(lockdir, LOCK_DIR);
	else
		strcpy(lockdir, path);

	strcpy(fifodir, FIFO_DIR);
}

void lockfile(void)
{
	int fd, error;
	struct flock lock;

	memset(fname, 0, 256);
	sprintf(fname, "%s/fence_manual.lock", lockdir);

	fd = open(fname, O_WRONLY | O_CREAT | O_NONBLOCK,
		  (S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH));
	if (fd < 0) {
		if (!quiet_flag)
			printf("failed: %s %s lockfile open error\n",
				prog_name, victim);
		exit(1);
	}

	lock.l_type = F_WRLCK;
	lock.l_start = 0;
	lock.l_whence = SEEK_SET;
	lock.l_len = 0;

	error = fcntl(fd, F_SETLKW, &lock);
	if (error < 0) {
		if (!quiet_flag)
			printf("failed: fcntl errno %d\n", errno);
		exit(1);
	}
}

void setup_fifo(void)
{
	int fd, error;

	memset(fname, 0, 256);
	sprintf(fname, "%s/fence_manual.fifo", fifodir);

	umask(0);

	error = mkfifo(fname, (S_IRUSR | S_IWUSR));
	if (error && errno != EEXIST) {
		if (!quiet_flag)
			printf("failed: %s mkfifo error\n", prog_name);
		exit(1);
	}

	fd = open(fname, O_RDONLY | O_NONBLOCK);
	if (fd < 0) {
		if (!quiet_flag)
			printf("failed: %s %s open error\n", prog_name, victim);
		exit(1);
	}

	fifo_fd = fd;
}

int check_ack(void)
{
	int error;
	char line[256], *mw, *ok;

	memset(line, 0, 256);

	error = read(fifo_fd, line, 256);
	if (error < 0)
		return error;
	if (error == 0)
		return 0;

	mw = strstr(line, "meatware");
	ok = strstr(line, "ok");

	if (!mw || !ok)
		return -ENOMSG;

	return 1;
}

int check_cluster(void)
{
	cman_handle_t ch;
	int i, error, rv = 0, count = 0;

	ch = cman_init(NULL);
	if (!ch)
		return 0;

	memset(&nodes, 0, sizeof(nodes));

	error = cman_get_nodes(ch, MAX_NODES, &count, nodes);
	if (error < 0)
		goto out;

	for (i = 0; i < count; i++) {
		if (strlen(nodes[i].cn_name) == strlen(victim) &&
		    !strncmp(nodes[i].cn_name, victim, strlen(victim))) {

			if (nodes[i].cn_member)
				rv = 1;
			break;
		}
	}
 out:
	cman_finish(ch);
	return rv;
}

void cleanup(void)
{
	memset(fname, 0, 256);
	sprintf(fname, "%s/fence_manual.fifo", fifodir);
	unlink(fname);
}

int main(int argc, char **argv)
{
	int rv;

	prog_name = argv[0];

	get_options(argc, argv);

	if (victim[0] == 0) {
		if (!quiet_flag)
			printf("failed: %s no node name\n", agent);
		exit(1);
	}

	lockfile();

	openlog("fence_manual", 0, LOG_DAEMON);

	syslog(LOG_CRIT, "Node %s needs to be reset before recovery can "
			 "procede.  Waiting for %s to rejoin the cluster "
			 "or for manual acknowledgement that it has been reset "
			 "(i.e. fence_ack_manual -n %s)\n",
			 victim, victim, victim);

	setup_fifo();

	for (;;) {
		rv = check_ack();
		if (rv)
			break;

		rv = check_cluster();
		if (rv)
			break;

		sleep(1);
	}

	if (rv < 0) {
		if (!quiet_flag)
			printf("failed: %s %s rv %d\n", prog_name, victim, rv);
		cleanup();
		exit(1);
	} else {
		if (!quiet_flag)
			printf("success: %s %s\n", prog_name, victim);
		cleanup();
		exit(0);
	}

	return 0;
}

