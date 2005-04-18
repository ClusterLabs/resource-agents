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

int set_local(int argc, char **argv);
int set_node(int argc, char **argv);
int ls_stop(int argc, char **argv);
int ls_terminate(int argc, char **argv);
int ls_start(int argc, char **argv);
int ls_finish(int argc, char **argv);
int ls_set_id(int argc, char **argv);
int ls_get_done(int argc, char **argv, int *event_nr);
int ls_poll_done(int argc, char **argv);
int ls_create(int argc, char **argv);
int ls_release(int argc, char **argv);
int ls_lock(int argc, char **argv);
int ls_unlock(int argc, char **argv);
int ls_convert(int argc, char **argv);


static void print_usage(void)
{
	printf("Usage:\n");
	printf("\n");
	printf("%s\n", prog_name);
	printf("\n");
	printf("set_local  <nodeid> <ipaddr> [<weight>]\n");
	printf("set_node   <nodeid> <ipaddr> [<weight>]\n");
	printf("\n");
	printf("stop       <ls_name>\n");
	printf("terminate  <ls_name>\n");
	printf("start      <ls_name> <event_nr> <type> <nodeid>...\n");
	printf("get_done   <ls_name>\n");
	printf("finish     <ls_name> <event_nr>\n");
	printf("poll_done  <ls_name> <event_nr>\n");
	printf("set_id     <ls_name> <id>\n");
	printf("\n");
	printf("create     <ls_name>\n");
	printf("release    <ls_name>\n");
	printf("lock       <ls_name> <res_name> <mode> [<flag>,...]\n");
	printf("unlock     <ls_name> <lkid>            [<flag>,...]\n");
	printf("convert    <ls_name> <lkid> <mode>     [<flag>,...]\n");
	printf("\n");
	printf("modes      NL,CR,CW,PR,PW,EX\n");
	printf("type       not used by dlm\n");
	printf("flags      NOQUEUE,CANCEL,QUECVT,CONVDEADLK,PERSISTENT,\n"
	       "           EXPEDITE,NOQUEUEBAST,HEADQUE,NOORDER\n");
	printf("           modes and flags are case insensitive\n");
}

static void print_version(void)
{
	printf("dlm_tool (built %s %s)\n", __DATE__, __TIME__);
	printf("%s\n", REDHAT_COPYRIGHT);
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
	int rv, x = argc, event_nr = 0;

	prog_name = argv[0];

	if (argc < 2) {
		print_usage();
		exit(EXIT_SUCCESS);
	}

	decode_arguments(&argc, argv);
	argv += (x - argc);

	if (strcmp(action, "set_local") == 0)
		rv = set_local(argc, argv);
	else if (strcmp(action, "set_node") == 0)
		rv = set_node(argc, argv);
	else if (strcmp(action, "stop") == 0)
		rv = ls_stop(argc, argv);
	else if (strcmp(action, "terminate") == 0)
		rv = ls_terminate(argc, argv);
	else if (strcmp(action, "start") == 0)
		rv = ls_start(argc, argv);
	else if (strcmp(action, "finish") == 0)
		rv = ls_finish(argc, argv);
	else if (strcmp(action, "set_id") == 0)
		rv = ls_set_id(argc, argv);
	else if (strcmp(action, "get_done") == 0) {
		rv = ls_get_done(argc, argv, &event_nr);
		printf("done event_nr %d\n", event_nr);
	} else if (strcmp(action, "poll_done") == 0)
		rv = ls_poll_done(argc, argv);
	else if (strcmp(action, "create") == 0)
		rv = ls_create(argc, argv);
	else if (strcmp(action, "release") == 0)
		rv = ls_release(argc, argv);
	else if (strcmp(action, "lock") == 0)
		rv = ls_lock(argc, argv);
	else if (strcmp(action, "unlock") == 0)
		rv = ls_unlock(argc, argv);
	else if (strcmp(action, "convert") == 0)
		rv = ls_convert(argc, argv);
	else
		die("unknown action: %s", action);

	if (rv)
		log_error("%s error %d errno %d", action, rv, errno);

	exit(EXIT_SUCCESS);
}
