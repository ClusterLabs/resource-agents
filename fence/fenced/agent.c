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
#include <fcntl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <string.h>
#include <errno.h>
#include <syslog.h>

#include "ccs.h"

#define MAX_METHODS		8
#define MAX_DEVICES		4
#define MAX_AGENT_ARGS_LEN	512

#define METHOD_NAME_PATH        "//cluster/nodes/node[@name=\"%s\"]/fence/method[%d]/@name"
#define DEVICE_NAME_PATH        "//cluster/nodes/node[@name=\"%s\"]/fence/method[@name=\"%s\"]/device[%d]/@name"
#define NODE_FENCE_ARGS_PATH    "//cluster/nodes/node[@name=\"%s\"]/fence/method[@name=\"%s\"]/device[@name=\"%s\"]/@*"
#define AGENT_NAME_PATH         "//cluster/fence_devices/device[@name=\"%s\"]/@agent"
#define FENCE_DEVICE_ARGS_PATH  "//cluster/fence_devices/device[@name=\"%s\"]/@*"


static int use_device(int cd, char *victim, char *method, char *device, int in);


static void display_agent_output(char *agent, int fd)
{
	char msg[512], buf[256];

	memset(msg, 0, sizeof(msg));
	memset(buf, 0, sizeof(buf));

	while (read(fd, buf, sizeof(buf)-1) > 0) {
		snprintf(msg, 256, "agent \"%s\" reports: ", agent);
		strcat(msg, buf);

		printf("%s\n", msg);
		syslog(LOG_ERR, msg);

		memset(buf, 0, sizeof(buf));
		memset(msg, 0, sizeof(msg));
	}
}

static int run_agent(char *agent, char *args)
{
	int pid, status, error, len = strlen(args);
	int pr_fd, pw_fd;  /* parent read/write file descriptors */
	int cr_fd, cw_fd;  /* child read/write file descriptors */
	int fd1[2];
	int fd2[2];

	cr_fd = cw_fd = pr_fd = pw_fd = -1;

	if (pipe(fd1))
		goto fail;
  	pr_fd = fd1[0];
  	cw_fd = fd1[1];

  	if (pipe(fd2))
    		goto fail;
  	cr_fd = fd2[0];
  	pw_fd = fd2[1];

	pid = fork();
	if (pid < 0)
    		goto fail;

	if (pid) {
		/* parent */

		fcntl(pr_fd, F_SETFL, fcntl(pr_fd, F_GETFL, 0) | O_NONBLOCK);

		error = write(pw_fd, args, len);
		if (error != len)
			goto fail;

		close(pw_fd);
		waitpid(pid, &status, 0);

		if (!WIFEXITED(status) || WEXITSTATUS(status)) {
			display_agent_output(agent, pr_fd);
			goto fail;
		}
	} else {
		/* child */

		close(1);
		dup(cw_fd);
		close(2);
		dup(cw_fd);
		close(0);
		dup(cr_fd);
		/* keep cw_fd open so parent can report all errors. */
		close(pr_fd);
		close(cr_fd);
		close(pw_fd);

		execlp(agent, agent, NULL);
		exit(EXIT_FAILURE);
	}

	close(pr_fd);
	close(cw_fd);
	close(cr_fd);
	close(pw_fd);
	return 0;

 fail:
	close(pr_fd);
	close(cw_fd);
	close(cr_fd);
	close(pw_fd);
	return -1;
}

static int make_args(int cd, char *victim, char *method, char *device,
		     char **args_out) {
	char path[256], *args, *str;
	int error;

	args = malloc(MAX_AGENT_ARGS_LEN);
	if (!args)
		return -ENOMEM;
	memset(args, 0, MAX_AGENT_ARGS_LEN);

	/* node-specific args for victim */

	memset(path, 0, 256);
	sprintf(path, NODE_FENCE_ARGS_PATH, victim, method, device);

	for (;;) {
		error = ccs_get(cd, path, &str);
		if (error || !str)
			break;

		if (!strncmp(str, "name=", 5)) {
			free(str);
			continue;
		}

		strcat(args, str);
		strcat(args, "\n");
		free(str);
	}

	/* device-specific args */

	memset(path, 0, 256);
	sprintf(path, FENCE_DEVICE_ARGS_PATH, device);

	for (;;) {
		error = ccs_get(cd, path, &str);
		if (error || !str)
			break;

		if (!strncmp(str, "name=", 5)) {
			free(str);
			continue;
		}

		strcat(args, str);
		strcat(args, "\n");
		free(str);
	}

	if (error) {
		free(args);
		args = NULL;
	}

	*args_out = args;
	return error;
}

/* return name of m'th method for nodes/<victim>/fence/ */

static int get_method(int cd, char *victim, int m, char **method)
{
	char path[256], *str = NULL;
	int error;

	memset(path, 0, 256);
	sprintf(path, METHOD_NAME_PATH, victim, m+1);

	error = ccs_get(cd, path, &str);
	*method = str;
	return error;
}

/* return name of d'th device under nodes/<victim>/fence/<method>/ */

static int get_device(int cd, char *victim, char *method, int d, char **device)
{
	char path[256], *str = NULL;
	int error;

	memset(path, 0, 256);
	sprintf(path, DEVICE_NAME_PATH, victim, method, d+1);

	error = ccs_get(cd, path, &str);
	*device = str;
	return error;
}

static int count_methods(int cd, char *victim)
{
	char path[256], *name;
	int error, i;

	for (i = 0; i < MAX_METHODS; i++) {
		memset(path, 0, 256);
		sprintf(path, METHOD_NAME_PATH, victim, i+1);

		error = ccs_get(cd, path, &name);
		if (error)
			break;
		free(name);
	}
	return i;
}

static int count_devices(int cd, char *victim, char *method)
{
	char path[256], *name;
	int error, i;

	for (i = 0; i < MAX_DEVICES; i++) {
		memset(path, 0, 256);
		sprintf(path, DEVICE_NAME_PATH, victim, method, i+1);

		error = ccs_get(cd, path, &name);
		if (error)
			break;
		free(name);
	}
	return i;
}

int dispatch_fence_agent(char *victim, int in)
{
	char *method, *device;
	int cd, num_methods, num_devices, m, d, error = -1;

	while ((cd = ccs_connect()) < 0)
		sleep(1);

	num_methods = count_methods(cd, victim);

	for (m = 0; m < num_methods; m++) {

		error = get_method(cd, victim, m, &method);
		if (error)
			continue;

		num_devices = count_devices(cd, victim, method);

		for (d = 0; d < num_devices; d++) {
			error = get_device(cd, victim, method, d, &device);
			if (error)
				break;

			error = use_device(cd, victim, method, device, in);
			if (error)
				break;

			free(device);
			device = NULL;
		}

		if (device)
			free(device);
		free(method);

		if (!error)
			break;
	}

	ccs_disconnect(cd);
	return error;
}

struct unfence_info {
	char *agent;
	char *option;
};

/* FIXME: find a better way of knowing which agents support unfencing
   than this static table, e.g. something we look up in cluster.conf. */

static struct unfence_info unfence_opts[] = {
	{ .agent = "fence_brocade",
	  .option = "option=enable", },

	{ .agent = "fence_sanbox2",
	  .option = "option=enable", },

	{ .agent = "fence_vixel",
	  .option = "option=enable", },
};

static int get_unfence_option(char *agent, char **option)
{
	int i, n = sizeof(unfence_opts) / sizeof(struct unfence_info);

	for (i = 0; i < n; i++) {
		if (!strcmp(agent, unfence_opts[i].agent)) {
			*option = unfence_opts[i].option;
			return 0;
		}
	}

	return -1;
}

/* replace the args string with one that has the unfence option appended */

static int append_unfence_option(char *agent, char **argsp)
{
	char *option = NULL, *args2 = NULL, *args = *argsp;
	int error, len;

	error = get_unfence_option(agent, &option);
	if (error)
		return error;

	len = strlen(args) + strlen(option) + 3;
	args2 = malloc(len);
	if (!args2)
		return -ENOMEM;
	memset(args2, 0, len);

	strncpy(args2, args, strlen(args));
	strcat(args2, option);
	strcat(args2, "\n");

	free(*argsp);
	*argsp = args2;
	return 0;
}

static int use_device(int cd, char *victim, char *method, char *device, int in)
{
	char path[256], *agent, *args = NULL;
	int error;

	memset(path, 0, 256);
	sprintf(path, AGENT_NAME_PATH, device);

	error = ccs_get(cd, path, &agent);
	if (error)
		goto out;

	error = make_args(cd, victim, method, device, &args);
	if (error)
		goto out_agent;
	
	if (in) {
		error = append_unfence_option(agent, &args);
		if (error)
			goto out_args;
	}

	error = run_agent(agent, args);

 out_args:
	free(args);
 out_agent:
	free(agent);
 out:
	return error;
}

