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

int do_command(struct dlm_member_ioctl *mi);


/*
 * ioctl interface only used for setting up addr/nodeid info
 * with set_local and set_node
 */

void init_mi(struct dlm_member_ioctl *mi)
{
	memset(mi, 0, sizeof(struct dlm_member_ioctl));

	mi->version[0] = DLM_MEMBER_VERSION_MAJOR;
	mi->version[1] = DLM_MEMBER_VERSION_MINOR;
	mi->version[2] = DLM_MEMBER_VERSION_PATCH;

	mi->data_size = sizeof(struct dlm_member_ioctl);
	mi->data_start = sizeof(struct dlm_member_ioctl);

	strcpy(mi->op, action);
}

static void set_ipaddr(struct dlm_member_ioctl *mi, char *ip)
{
	struct sockaddr_in sin;
	memset(&sin, 0, sizeof(sin));
	sin.sin_family = AF_INET;
	inet_pton(AF_INET, ip, &sin.sin_addr);
	memcpy(mi->addr, &sin, sizeof(sin));
}

static void set_node(int argc, char **argv)
{
	struct dlm_member_ioctl mi;

	if (argc < 2 || argc > 3)
		die("%s invalid arguments", action);

	init_mi(&mi);
	mi.nodeid = atoi(argv[0]);
	set_ipaddr(&mi, argv[1]);
	if (argc > 2)
		mi.weight = atoi(argv[2]);
	do_command(&mi);
}

static void set_local(int argc, char **argv)
{
	struct dlm_member_ioctl mi;

	if (argc < 2 || argc > 3)
		die("%s invalid arguments", action);

	init_mi(&mi);
	mi.nodeid = atoi(argv[0]);
	set_ipaddr(&mi, argv[1]);
	if (argc > 2)
		mi.weight = atoi(argv[2]);
	do_command(&mi);
}

/*
 * sysfs interface used for lockspace control (stop/start/finish/terminate),
 * for setting lockspace id, lockspace members
 */

static void stop(int argc, char **argv)
{
	char fname[512];
	int rv, fd;

	if (argc != 1)
		die("%s invalid arguments", action);

	sprintf(fname, "/sys/kernel/dlm/%s/stop", argv[0]);

	fd = open(fname, O_RDWR);
	if (fd < 0) {
		printf("%s open error %d %d\n", action, fd, errno);
		exit(1);
	}

	rv = write(fd, "1", strlen("1"));
	if (rv != 1) {
		printf("%s write error %d %d\n", action, rv, errno);
		exit(1);
	}
}

static void terminate(int argc, char **argv)
{
	char fname[512];
	int rv, fd;

	if (argc != 1)
		die("%s invalid arguments", action);

	sprintf(fname, "/sys/kernel/dlm/%s/terminate", argv[0]);

	fd = open(fname, O_RDWR);
	if (fd < 0) {
		printf("%s open error %d %d\n", action, fd, errno);
		exit(1);
	}

	rv = write(fd, "1", strlen("1"));
	if (rv != 1) {
		printf("%s write error %d %d\n", action, rv, errno);
		exit(1);
	}
}

static void finish(int argc, char **argv)
{
	char fname[512];
	int rv, fd;

	if (argc != 2)
		die("%s invalid arguments", action);

	sprintf(fname, "/sys/kernel/dlm/%s/finish", argv[0]);

	fd = open(fname, O_RDWR);
	if (fd < 0) {
		printf("%s open error %d %d\n", action, fd, errno);
		exit(1);
	}

	rv = write(fd, argv[1], strlen(argv[1]));
	if (rv != strlen(argv[1])) {
		printf("%s write error %d %d\n", action, rv, errno);
		exit(1);
	}
}

static void start(int argc, char **argv)
{
	char fname[512];
	int i, rv, fd, len = 0;
	char *p;

	if (argc < 3)
		die("%s invalid arguments", action);

	/* first set up new members */

	for (i = 2; i < argc; i++)
		len += strlen(argv[i]) + 1;
	len -= 1;

	p = malloc(len);
	if (!p) {
		printf("%s malloc error\n", action);
		exit(1);
	}
	memset(p, 0, len);

	for (i = 2; i < argc; i++) {
		if (i != 2)
			strcat(p, " ");
		strcat(p, argv[i]);
	}

	sprintf(fname, "/sys/kernel/dlm/%s/members", argv[0]);

	fd = open(fname, O_RDWR);
	if (fd < 0) {
		printf("%s open error %s %d %d\n", action, fname, fd, errno);
		exit(1);
	}

	printf("write to %s %d: \"%s\"\n", fname, len, p);
	rv = write(fd, p, len);
	if (rv != len) {
		printf("%s write error %s %d %d\n", action, fname, rv, errno);
		exit(1);
	}

	free(p);
	close(fd);

	/* second do the start */

	sprintf(fname, "/sys/kernel/dlm/%s/start", argv[0]);

	fd = open(fname, O_RDWR);
	if (fd < 0) {
		printf("%s open error %s %d %d\n", action, fname, fd, errno);
		exit(1);
	}

	printf("write to %s: \"%s\"\n", fname, argv[1]);
	len = strlen(argv[1]);
	rv = write(fd, argv[1], len);
	if (rv != len) {
		printf("%s write error %s %d %d\n", action, fname, rv, errno);
		exit(1);
	}
}

static void set_id(int argc, char **argv)
{
	char fname[512];
	int len, fd, rv;

	if (argc != 2)
		die("%s invalid arguments", action);

	sprintf(fname, "/sys/kernel/dlm/%s/id", argv[0]);

	fd = open(fname, O_RDWR);
	if (fd < 0) {
		printf("%s open error %d %d\n", action, fd, errno);
		exit(1);
	}

	len = strlen(argv[1]);
	rv = write(fd, argv[1], len);
	if (rv != len) {
		printf("%s write error %d %d\n", action, rv, errno);
		exit(1);
	}
}

static void poll_done(int argc, char **argv)
{
	/* FIXME: loop reading /sys/kernel/dlm/<ls>/done until it
	   equals the given event_nr */
	printf("not yet implemented\n");
}

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
	printf("start      <ls_name> <event_nr> <nodeid>...\n");
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
	printf("flags      NOQUEUE,CANCEL,QUECVT,CONVDEADLK,PERSISTENT,\n"
	       "           EXPEDITE,NOQUEUEBAST,HEADQUE,NOORDER\n");
	printf("           modes and flags are case insensitive\n");
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
	int x = argc;

	prog_name = argv[0];

	if (argc < 2) {
		print_usage();
		exit(EXIT_SUCCESS);
	}

	decode_arguments(&argc, argv);
	argv += (x - argc);

	if (strcmp(action, "set_local") == 0)
		set_local(argc, argv);
	else if (strcmp(action, "set_node") == 0)
		set_node(argc, argv);
	else if (strcmp(action, "stop") == 0)
		stop(argc, argv);
	else if (strcmp(action, "terminate") == 0)
		terminate(argc, argv);
	else if (strcmp(action, "start") == 0)
		start(argc, argv);
	else if (strcmp(action, "finish") == 0)
		finish(argc, argv);
	else if (strcmp(action, "set_id") == 0)
		set_id(argc, argv);
	else if (strcmp(action, "poll_done") == 0)
		poll_done(argc, argv);
	else if (strcmp(action, "create") == 0)
		ls_create(argc, argv);
	else if (strcmp(action, "release") == 0)
		ls_release(argc, argv);
	else if (strcmp(action, "lock") == 0)
		ls_lock(argc, argv);
	else if (strcmp(action, "unlock") == 0)
		ls_unlock(argc, argv);
	else if (strcmp(action, "convert") == 0)
		ls_convert(argc, argv);
	else
		die("unknown action: %s", action);

	exit(EXIT_SUCCESS);
}
