/******************************************************************************
*******************************************************************************
**
**  Copyright (C) 2005 Red Hat, Inc.  All rights reserved.
**
**  This copyrighted material is made available to anyone wishing to use,
**  modify, copy, or redistribute it subject to the terms and conditions
**  of the GNU General Public License v.2.
**
*******************************************************************************
******************************************************************************/

#include "dlm_tool.h"
#include "copyright.cf"

char *prog_name;
char *action = NULL;
int debug = FALSE;

void open_control(void);
int do_command(struct dlm_member_ioctl *mi);


static void status(struct dlm_member_ioctl *mi, int argc, char **argv)
{
	do_command(mi);
}

static void set_ipaddr(struct dlm_member_ioctl *mi, char *ip)
{
	struct sockaddr_in sin;
	memset(&sin, 0, sizeof(sin));
	sin.sin_family = AF_INET;
	inet_pton(AF_INET, ip, &sin.sin_addr);
	memcpy(mi->addr, &sin, sizeof(sin));
}

static void set_node(struct dlm_member_ioctl *mi, int argc, char **argv)
{
	if (argc != 3)
		die("%s invalid arguments", action);
	mi->nodeid = atoi(argv[0]);
	mi->weight = atoi(argv[2]);
	set_ipaddr(mi, argv[1]);
	do_command(mi);
}

static void set_local(struct dlm_member_ioctl *mi, int argc, char **argv)
{
	if (argc != 2)
		die("%s invalid arguments", action);
	mi->nodeid = atoi(argv[0]);
	set_ipaddr(mi, argv[1]);
	do_command(mi);
}

static void stop(struct dlm_member_ioctl *mi, int argc, char **argv)
{
	if (argc != 1)
		die("%s invalid arguments", action);
	strcpy(mi->name, argv[0]);
	do_command(mi);
}

static void terminate(struct dlm_member_ioctl *mi, int argc, char **argv)
{
	if (argc != 1)
		die("%s invalid arguments", action);
	strcpy(mi->name, argv[0]);
	do_command(mi);
}

static void finish(struct dlm_member_ioctl *mi, int argc, char **argv)
{
	if (argc != 2)
		die("%s invalid arguments", action);
	strcpy(mi->name, argv[0]);
	mi->finish_event = atoi(argv[1]);
	do_command(mi);
}

static void start(struct dlm_member_ioctl *mi_in, int argc, char **argv)
{
	struct dlm_member_ioctl *mi;
	int i, len = sizeof(struct dlm_member_ioctl) + 1;
	char *str;

	if (argc < 4)
		die("%s invalid arguments", action);

	for (i = 3; i < argc; i++)
		len += strlen(argv[i]) + 1;

	mi = malloc(len);
	mi->data_size = len;
	memcpy(mi, mi_in, sizeof(struct dlm_member_ioctl));
	strcpy(mi->name, argv[0]);
	mi->start_event = atoi(argv[1]);
	mi->global_id = atoi(argv[2]);

	str = (char *) mi + sizeof(struct dlm_member_ioctl);

	for (i = 3; i < argc; i++) {
		strcat(str, argv[i]);
		strcat(str, " ");
	}

	do_command(mi);

	free(mi);
}

static void poll_done(struct dlm_member_ioctl *mi, int argc, char **argv)
{
	int event_nr;

	if (argc != 2)
		die("poll_done invalid arguments");

	strcpy(mi->op, "status");
	strcpy(mi->name, argv[0]);
	event_nr = atoi(argv[1]);

	while (1) {
		status(mi, 0, NULL);
		if (mi->startdone_event == event_nr)
			break;
		sleep(1);
	}
}

static void print_usage(void)
{
	printf("Usage:\n");
	printf("\n");
	printf("%s\n", prog_name);
	printf("\n");
	printf("set_local  <nodeid> <ipaddr>\n");
	printf("set_node   <nodeid> <ipaddr> <weight>\n");
	printf("stop       <ls_name>\n");
	printf("terminate  <ls_name>\n");
	printf("start      <ls_name> <event_nr> <global_id> <nodeid>...\n");
	printf("finish     <ls_name> <event_nr>\n");
	printf("poll_done  <ls_name> <event_nr>\n");
}

static void print_version(void)
{
	printf("dlm_tool (built %s %s)\n", __DATE__, __TIME__);
	printf("%s\n", REDHAT_COPYRIGHT);
	printf("tool version: %d.%d.%d\n",
	       DLM_MEMBER_VERSION_MAJOR,
	       DLM_MEMBER_VERSION_MINOR,
	       DLM_MEMBER_VERSION_PATCH);

	/* FIXME: get version from kernel */
}

static void decode_arguments(int *argc, char **argv)
{
	int cont = TRUE;
	int optchar;

	while (cont) {
		optchar = getopt(*argc, argv, "DhV");

		switch (optchar) {
		case 'D':
			debug = TRUE;
			break;

		case 'h':
			print_usage();
			exit(EXIT_SUCCESS);

		case 'V':
			print_version();
			exit(EXIT_SUCCESS);

		case EOF:
			cont = FALSE;
			break;

		default:
			die("unknown option: %c", optchar);
		};
	}

	if (optind < *argc) {
		action = argv[optind];
		optind++;
	} else
		die("no action specified");

	*argv += optind;
	*argc -= optind;
}

int main(int argc, char **argv)
{
	struct dlm_member_ioctl mi;
	int x = argc;

	prog_name = argv[0];

	if (argc < 2) {
		print_usage();
		exit(EXIT_SUCCESS);
	}

	decode_arguments(&argc, argv);
	argv += (x - argc);

	open_control();

	mi.version[0] = DLM_MEMBER_VERSION_MAJOR;
	mi.version[1] = DLM_MEMBER_VERSION_MINOR;
	mi.version[2] = DLM_MEMBER_VERSION_PATCH;
	mi.data_size = sizeof(mi);
	mi.data_start = sizeof(mi);

	strcpy(mi.op, action);

	if (strcmp(action, "status") == 0)
		status(&mi, argc, argv);
	else if (strcmp(action, "set_local") == 0)
		set_local(&mi, argc, argv);
	else if (strcmp(action, "set_node") == 0)
		set_node(&mi, argc, argv);
	else if (strcmp(action, "stop") == 0)
		stop(&mi, argc, argv);
	else if (strcmp(action, "terminate") == 0)
		terminate(&mi, argc, argv);
	else if (strcmp(action, "start") == 0)
		start(&mi, argc, argv);
	else if (strcmp(action, "finish") == 0)
		finish(&mi, argc, argv);
	else if (strcmp(action, "poll_done") == 0)
		poll_done(&mi, argc, argv);
	else
		die("unknown action: %s", action);

	exit(EXIT_SUCCESS);
}
