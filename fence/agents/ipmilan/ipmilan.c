/** @file
 * clumanager 1.2.x linux-cluster fence and/or GFS fence
 * module for Intel/Bull/Dell Tiger4 machines via IPMI over lan.
 * (Probably works with anything ipmitool can control, though.)
 *
 * Note: REQUIRES ipmitool to operate.  On certain machines, the hardware
 * manufacturer provides this tool for you.  Otherwise, check:
 *
 *  http://ipmitool.sourceforge.net
 *
 * Copyright 2005-2007 Red Hat, Inc.
 *  author: Lon Hohberger <lhh at redhat.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 * 
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 * 
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <string.h>
#include <errno.h>
#include <limits.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <libintl.h>

/* fenced doesn't use the remote calls */
#define ST_STATUS 0
#define ST_POWERON 1
#define ST_POWEROFF 2
#define ST_GENERIC_RESET 3

#define log(lvl, fmt, args...) fprintf(stderr, fmt, ##args)
#include <libgen.h>
#include "copyright.cf"

#include "expect.h"

#define IPMIID "IPMI over LAN driver"
#define NOTIPMI "Destroyed IPMI over LAN driver"


#define dbg_printf(i, lvl, fmt, args...) \
do { \
	if ( (i)->i_verbose >= lvl) { \
		printf(fmt, ##args); \
		fflush(stdout); \
	} \
} while (0)



struct ipmi {
	char *i_id;
	const char *i_ipmitool;
	char *i_host;
	char *i_user;
	char *i_authtype;
	char *i_password;
	int i_rdfd;
	int i_wrfd;
	pid_t i_pid;
	int i_config;
	int i_verbose;
	int i_lanplus;
};


/*
   Supported installation paths
 */
const char *ipmitool_paths[] = {
	"/usr/local/bull/NSMasterHW/bin/ipmitool",
	"/usr/bin/ipmitool",
	"/usr/sbin/ipmitool",
	"/bin/ipmitool",
	"/sbin/ipmitool",
	"/usr/local/bin/ipmitool",
	"/usr/local/sbin/ipmitool",
	NULL
};


static struct Etoken power_on_complete[] = {
	{"Password:", EPERM, 0},
	{"Unable to establish LAN", EAGAIN, 0},	/* Retry */
	{"IPMI mutex", EFAULT, 0},	/* Death */
	{"Up/On", 0, 0},
	{NULL, 0, 0}
};

static struct Etoken power_off_complete[] = {
	{"Password:", EPERM, 0},
	{"Unable to establish LAN", EAGAIN, 0},	/* Retry */
	{"IPMI mutex", EFAULT, 0},	/* Death */
	{"Down/Off", 0, 0},
	{NULL, 0, 0}
};


#define STATE_OFF 4096
#define STATE_ON  8192
static struct Etoken power_status[] = {
	{"Password:", EPERM, 0},
	{"Unable to establish LAN", EAGAIN, 0},	/* Retry */
	{"IPMI mutex", EFAULT, 0},	/* Death */
	{"Chassis Power is off", STATE_OFF, 0},
	{"Chassis Power is on", STATE_ON, 0},
	{NULL, 0, 0}
};


/*
   Search for ipmitool
 */
static const char *
ipmitool_path(void)
{
	char *p;
	int x = 0;
	struct stat sb;

	for (x = 0; ipmitool_paths[x]; x++) {
		p = (char *)ipmitool_paths[x];
		if (stat(p, &sb) != 0)
			continue;

		if (!S_ISREG(sb.st_mode))
			continue;

		/* executable? */
		if ((sb.st_mode & S_IXUSR) == 0)
			continue;

		return (const char *)p;
	}

	return NULL;
}


static int
build_cmd(char *command, size_t cmdlen, struct ipmi *ipmi, int op)
{
	char cmd[2048];
	char arg[2048];
	int x;

	/* Store path */
	if (ipmi->i_lanplus) {
		snprintf(cmd, sizeof(cmd), "%s -I lanplus -H %s", 
				ipmi->i_ipmitool, ipmi->i_host);
	} else {
		snprintf(cmd, sizeof(cmd), "%s -I lan -H %s", ipmi->i_ipmitool,
				ipmi->i_host);
	}

	if (ipmi->i_user) {
		snprintf(arg, sizeof(arg), " -U %s", ipmi->i_user);
		strncat(cmd, arg, sizeof(cmd) - strlen(arg));
	}

	if (ipmi->i_authtype) {
		snprintf(arg, sizeof(arg), " -A %s", ipmi->i_authtype);
		strncat(cmd, arg, sizeof(cmd) - strlen(arg));
	}

	if (ipmi->i_password) {
		snprintf(arg, sizeof(arg), " -P %s", ipmi->i_password);
		strncat(cmd, arg, sizeof(cmd) - strlen(arg));
	} else {
		snprintf(arg, sizeof(arg), " -P ''");
		strncat(cmd, arg, sizeof(cmd) - strlen(arg));
	}

	/* Tack on the -v flags for ipmitool; in most cases, i_verbose
	   will be 0 */
	for (x = 0; x < ipmi->i_verbose; x++) {
		snprintf(arg, sizeof(arg), " -v");
		strncat(cmd, arg, sizeof(cmd) - strlen(arg));
	}

	switch(op) {
	case ST_POWERON:
		snprintf(arg, sizeof(arg),
			 "%s chassis power on", cmd);
		break;
	case ST_POWEROFF:
		snprintf(arg, sizeof(arg),
			 "%s chassis power off", cmd);
		break;
	case ST_STATUS:
		snprintf(arg, sizeof(arg),
			 "%s chassis power status", cmd);
		break;
	}

	strncpy(command, arg, cmdlen);
	return 0;
}


static int
ipmi_spawn(struct ipmi *ipmi, const char *cmd)
{
	dbg_printf(ipmi, 1, "Spawning: '%s'...\n", cmd);
	if (!ipmi) {
		errno = EINVAL;
		return -1;
	}

	if (ipmi->i_pid != -1)  {
		dbg_printf(ipmi, 1, "Can't spawn: PID %d running\n",
			   (int)ipmi->i_pid);
		errno = EINPROGRESS;
		return -1;
	}

	if ((ipmi->i_pid = StartProcess(cmd, &ipmi->i_rdfd,
					&ipmi->i_wrfd,
					EXP_STDERR|EXP_NOCTTY)) >= 0) {
		dbg_printf(ipmi, 2, "Spawned: '%s' - PID %d\n", cmd,
			   (int)ipmi->i_pid);
		return 0;
	}
	return -1;
}


static int
ipmi_reap(struct ipmi *ipmi)
{
	if (ipmi->i_pid >= 0) {
		dbg_printf(ipmi, 2, "Reaping pid %d\n", ipmi->i_pid);
		kill(ipmi->i_pid, 9);
		waitpid(ipmi->i_pid, NULL, 0);
	}
	ipmi->i_pid = -1;
	if (ipmi->i_rdfd >= 0) {
		close(ipmi->i_rdfd);
		ipmi->i_rdfd = -1;
	}
	if (ipmi->i_wrfd >= 0) {
		close(ipmi->i_wrfd);
		ipmi->i_wrfd = -1;
	}
	return 0;
}


static int
ipmi_expect(struct ipmi *ipmi, struct Etoken *toklist, int timeout)
{
	int ret;
	char buf[32768]; /* XX hope this is enough */

	dbg_printf(ipmi, 3, "Looking for: \n");
	for (ret = 0; toklist[ret].string; ret++) {
		dbg_printf(ipmi, 3, "    '%s', val = %d\n",
			   toklist[ret].string,
			   toklist[ret].toktype);
	}

	ret = ExpectToken(ipmi->i_rdfd, toklist, timeout, buf, sizeof(buf));
	dbg_printf(ipmi, 3, "ExpectToken returned %d\n", ret);
	if (ret == -1) {
		ret = errno;
		dbg_printf(ipmi, 3, "ExpectToken failed.  Info returned:\n");
		dbg_printf(ipmi, 3, ">>>>>\n%s\n<<<<<\nError = %d (%s)\n",
			   buf,
			   ret,
			   strerror(ret));
	}

	return ret;
}


static int
ipmi_op(struct ipmi *ipmi, int op, struct Etoken *toklist)
{
	char cmd[2048];
	int retries = 5; 
	int ret;

	build_cmd(cmd, sizeof(cmd), ipmi, op);

	if (ipmi_spawn(ipmi, cmd) != 0)
		return -1;
	ret = ipmi_expect(ipmi, toklist, 120);
	ipmi_reap(ipmi);

	while ((ret == EAGAIN || ret == ETIMEDOUT) && retries > 0) {
		dbg_printf(ipmi, 3, "Sleeping 5 ...\n");
		sleep(5);
		dbg_printf(ipmi, 1, "Retrying ...\n");
		--retries;
		
		if (ipmi_spawn(ipmi, cmd) != 0)
			return -1;
		ret = ipmi_expect(ipmi, toklist, 120);
		if (ret == EFAULT) {
			/* Doomed. */
			break;
		}
		ipmi_reap(ipmi);
	}

	if (ret == EFAULT) {
		log(LOG_CRIT, "ipmilan: ipmitool failed to create "
		    "mutex; unable to complete operation\n");
		return ret;
	}

	if (ret == EAGAIN) {
		/*!!! Still couldn't get through?! */
		log(LOG_WARNING,
		    "ipmilan: Failed to connect after 30 seconds\n");
	}

	return ret;
}


static int
ipmi_off(struct ipmi *ipmi)
{
	int ret, retries = 5;

	ret = ipmi_op(ipmi, ST_STATUS, power_status);
	switch(ret) {
	case STATE_ON:
		break;
	case STATE_OFF:
		return 0;
	default:
		return ret;
	}

	ret = ipmi_op(ipmi, ST_POWEROFF, power_off_complete);
	if (ret != 0)
		return ret;

	while (retries>=0) {
		sleep(5);
		--retries;
		ret = ipmi_op(ipmi, ST_STATUS, power_status);

		switch(ret) {
		case STATE_OFF:
			return 0;
		case EFAULT:
			/* We're done. */
			retries = 0;
			break;
		case STATE_ON:
		default:
			continue;
		}
	}
	log(LOG_WARNING, "ipmilan: Power still on\n");

	return ret;
}


static int
ipmi_on(struct ipmi *ipmi)
{
	int ret, retries = 5; 

	ret = ipmi_op(ipmi, ST_STATUS, power_status);
	switch(ret) {
	case STATE_ON:
		return 0;
	case STATE_OFF:
		break;
	default:
		return ret;
	}

	ret = ipmi_op(ipmi, ST_POWERON, power_on_complete);
	if (ret != 0)
		return ret;

	while (retries>=0) {
		sleep(5);
		--retries;
		ret = ipmi_op(ipmi, ST_STATUS, power_status);

		switch(ret) {
		case STATE_ON:
			return 0;
		case EFAULT:
			/* We're done. */
			retries = 0;
			break;
		case STATE_OFF:
		default:
			continue;
		}
	}
	log(LOG_WARNING, "ipmilan: Power still off\n");

	return ret;
}


/**
  Squash all our private data
 */
static void
ipmi_destroy(struct ipmi *i)
{
	ipmi_reap(i);
	if (i->i_user) {
		free(i->i_user);
		i->i_user = NULL;
	}
	if (i->i_password) {
		free(i->i_password);
		i->i_password= NULL;
	}
	if (i->i_host) {
		free(i->i_host);
		i->i_host = NULL;
	}
	i->i_config = 0;
	i->i_id = NOTIPMI;
}


/**
  Multipurpose initializer.  Used to either create a new, blank ipmi,
  or update an existing one, or both.
 */
static struct ipmi *
ipmi_init(struct ipmi *i, char *host, char *authtype,
	  char *user, char *password, int lanplus, int verbose)
{
	const char *p;

	if (!i || !i->i_ipmitool)
		p = ipmitool_path();
	else
		p = i->i_ipmitool;

	if (!p) {
		log(LOG_WARNING, "ipmilan: ipmitool not found!\n");
		return NULL;
	}

	if (!i)
		i = malloc (sizeof(*i));
	if (!i)
		return NULL;

	if (host && strlen(host)) {
		i->i_host = strdup(host);
		if (!i->i_host) {
			free(i);
			return NULL;
		}
	} else
		i->i_host = NULL;

	if (password && strlen(password)) {
		i->i_password = strdup(password);
		if (!i->i_password) {
			free(i->i_host);
			free(i);
			return NULL;
		}
	} else
		i->i_password = NULL;

	if (authtype && strlen(authtype)) {
		i->i_authtype = strdup(authtype);
		if (!i->i_authtype) {
			free(i->i_host);
			if (i->i_password)
				free(i->i_password);
			free(i);
			return NULL;
		}
	} else
		i->i_authtype = NULL;


	if (user && strlen(user)) {
		i->i_user= strdup(user);
		if (!i->i_user) {
			free(i->i_host);
			if (i->i_authtype)
				free(i->i_authtype);
			if (i->i_password)
				free(i->i_password);
			free(i);
			return NULL;
		}
	} else
		i->i_user = NULL;
	i->i_ipmitool = p;
	i->i_rdfd = -1;
	i->i_wrfd = -1;
	i->i_pid = -1;
	i->i_id = IPMIID;
	i->i_verbose = verbose;
	i->i_lanplus = lanplus;

	return i;
}


/**
   Remove leading and trailing whitespace from a line of text.
 */
int
cleanup(char *line, size_t linelen)
{
	char *p;
	size_t x;

	/* Remove leading whitespace. */
	p = line;
	for (x = 0; x <= linelen; x++) {
		switch (line[x]) {
		case '\t':
		case ' ':
			break;
		case '\n':
		case '\r':
			return -1;
		default:
			goto eol;
		}
	}
eol:
	/* Move the remainder down by as many whitespace chars as we
	   chewed up */
	if (x)
		memmove(p, &line[x], linelen-x);

	/* Remove trailing whitespace. */
	for (x=0; x <= linelen; x++) {
		switch(line[x]) {
		case '\t':
		case ' ':
		case '\r':
		case '\n':
			line[x] = 0;
		case 0:
		/* End of line */
			return 0;
		}
	}

	return -1;
}


/**
   Parse args from stdin.  Dev + devlen + op + oplen must be valid.
 */
int
get_options_stdin(char *ip, size_t iplen,
		  char *authtype, size_t atlen,
		  char *passwd, size_t pwlen,
		  char *pwd_script, size_t pwd_script_len,
		  char *user, size_t userlen,
		  char *op, size_t oplen,
		  int *lanplus, int *verbose)
{
	char in[256];
	int line = 0;
	char *name, *val;

	op[0] = 0;

	while (fgets(in, sizeof(in), stdin)) {
		++line;

		if (in[0] == '#')
			continue;

		if (cleanup(in, sizeof(in)) == -1)
			continue;

		name = in;
		if ((val = strchr(in, '='))) {
			*val = 0;
			++val;
		}

		if (!strcasecmp(name, "agent")) {
			/* Used by fenced? */
		} else if (!strcasecmp(name, "verbose")) {
			*verbose = 1;
		} else if (!strcasecmp(name, "ipaddr")) {
			/* IP address to use.  E.g. 10.1.1.2 */
			if (val)
				strncpy(ip, val, iplen);
			else
				ip[0] = 0;

		} else if (!strcasecmp(name, "auth")) {
			/* Authtype to use */
			if (val)
				strncpy(authtype, val, atlen);
			else
				authtype[0] = 0;

		} else if (!strcasecmp(name, "passwd")) {
			/* password */
			if (val)
				strncpy(passwd, val, pwlen);
			else
				passwd[0] = 0;

		} else if (!strcasecmp(name, "passwd_script")) {
			if (val) {
				strncpy(pwd_script, val, pwd_script_len);
				pwd_script[pwd_script_len - 1] = '\0';
			} else
				pwd_script[0] = '\0';
		} else if (!strcasecmp(name, "user") || !strcasecmp(name, "login")) {
			/* username */
			if (val)
				strncpy(user, val, userlen);
			else
				user[0] = 0;
		} else if (!strcasecmp(name, "lanplus")) {
			(*lanplus) = 1;
		} else if (!strcasecmp(name, "option") ||
			   !strcasecmp(name, "operation") ||
			   !strcasecmp(name, "action")) {
			if (val)
				strncpy(op, val, oplen);
			else
				op[0] = 0;
		}
	}

	return 0;
}


/**
   Print a message to stderr and call exit(1).
 */
void
fail_exit(char *msg)
{
	fprintf(stderr, "failed: %s\n", msg);
	exit(1);
}

void
usage_exit(char *pname)
{
printf("usage: %s <options>\n", pname);
printf("   -A <authtype>  IPMI Lan Auth type (md5, password, or none)\n");
printf("   -a <ipaddr>    IPMI Lan IP to talk to\n");
printf("   -i <ipaddr>    IPMI Lan IP to talk to (deprecated, use -i)\n");
printf("   -p <password>  Password (if required) to control power on\n"
       "                  IPMI device\n");
printf("   -P             Use Lanplus\n");
printf("   -S <path>      Script to retrieve password (if required)\n");
printf("   -l <login>     Username/Login (if required) to control power\n"
       "                  on IPMI device\n");
printf("   -o <op>        Operation to perform.\n");
printf("                  Valid operations: on, off, reboot, status\n");
printf("   -V             Print version and exit\n");
printf("   -v             Verbose mode\n\n");
printf("If no options are specified, the following options will be read\n");
printf("from standard input (one per line):\n\n");
printf("   auth=<auth>           Same as -A\n");
printf("   ipaddr=<#>            Same as -a\n");
printf("   passwd=<pass>         Same as -p\n");
printf("   passwd_script=<path>  Same as -S\n");
printf("   lanplus               Same as -P\n");
printf("   login=<login>         Same as -u\n");
printf("   option=<op>           Same as -o\n");
printf("   operation=<op>        Same as -o\n");
printf("   action=<op>           Same as -o\n");
printf("   verbose               Same as -v\n\n");
	exit(1);
}


int
main(int argc, char **argv)
{
	extern char *optarg;
	int opt, ret = -1;
	char authtype[64];
	char ip[64];
	char passwd[64];
	char user[64];
	char op[64];
	char pwd_script[PATH_MAX] = { 0, };
	int lanplus=0;
	int verbose=0;
	char *pname = basename(argv[0]);
	struct ipmi *i;

	memset(ip, 0, sizeof(ip));
	memset(authtype, 0, sizeof(authtype));
	memset(passwd, 0, sizeof(passwd));
	memset(user, 0, sizeof(user));
	memset(op, 0, sizeof(op));

	if (argc > 1) {
		/*
		   Parse command line options if any were specified
		 */
		while ((opt = getopt(argc, argv, "A:a:i:l:p:S:Po:vV?hH")) != EOF) {
			switch(opt) {
			case 'A':
				/* Auth type */
				strncpy(authtype, optarg, sizeof(authtype));
				break;
			case 'a':
			case 'i':
				/* IP address */
				strncpy(ip, optarg, sizeof(ip));
				break;
			case 'l':
				/* user / login */
				strncpy(user, optarg, sizeof(user));
				break;
			case 'p':
				/* password */
				strncpy(passwd, optarg, sizeof(passwd));
				break;
			case 'P':
				lanplus = 1;
				break;
			case 'S':
				strncpy(pwd_script, optarg, sizeof(pwd_script));
				pwd_script[sizeof(pwd_script) - 1] = '\0';
				break;
			case 'o':
				/* Operation */
				strncpy(op, optarg, sizeof(op));
				break;
			case 'v':
				verbose++;
				break;
			case 'V':
        			printf("%s %s (built %s %s)\n", pname,
				       RELEASE_VERSION,
               				__DATE__, __TIME__);
        			printf("%s\n",
				       REDHAT_COPYRIGHT);
				return 0;
			default:
				usage_exit(pname);
			}
		}
	} else {
		/*
		   No command line args?  Get stuff from stdin
		 */
		if (get_options_stdin(ip, sizeof(ip),
				      authtype, sizeof(authtype),
				      passwd, sizeof(passwd),
					  pwd_script, sizeof(pwd_script),
				      user, sizeof(user),
				      op, sizeof(op), &lanplus, &verbose) != 0)
			return 1;
	}

	if (pwd_script[0] != '\0') {
		char pwd_buf[1024];
		FILE *fp;
		fp = popen(pwd_script, "r");
		if (fp != NULL) {
			ssize_t len = fread(pwd_buf, 1, sizeof(pwd_buf), fp);
			if (len > 0) {
				char *p;
				p = strchr(pwd_buf, '\n');
				if (p != NULL)
					*p = '\0';
				p = strchr(pwd_buf, '\r');
				if (p != NULL)
					*p = '\0';
				strncpy(passwd, pwd_buf, sizeof(passwd));
				passwd[sizeof(passwd) - 1] = '\0';
			}
			pclose(fp);
		}
	}

	/*
	   Validate the operating parameters
	 */
	if (strlen(ip) == 0)
		fail_exit("no IP address specified");

	if (!strlen(op))
		snprintf(op,sizeof(op), "reboot");

	if (strcasecmp(op, "off") && strcasecmp(op, "on") &&
	    strcasecmp(op, "status") && strcasecmp(op, "reboot")) {
		fail_exit("operation must be 'on', 'off', 'status', "
			  "or 'reboot'");
	}

	if (strlen(authtype) &&
	    strcasecmp(authtype, "md5") &&
	    strcasecmp(authtype, "password") &&
	    strcasecmp(authtype, "none")) {
		fail_exit("authtype, if included, must be 'md5', 'password',"
			  " 'none'.");
	}


	/* Ok, set up the IPMI struct */
	i = ipmi_init(NULL, ip, authtype, user, passwd, lanplus, verbose);
	if (!i)
		fail_exit("Failed to initialize\n");

	/*
	   Perform the requested operation
	 */
	if (!strcasecmp(op, "reboot")) {
		printf("Rebooting machine @ IPMI:%s...", ip);
		fflush(stdout);
		ret = ipmi_off(i);
		if (ret != 0)
			goto out;
		ret = ipmi_on(i);

	} else if (!strcasecmp(op, "on")) {
		printf("Powering on machine @ IPMI:%s...", ip);
		fflush(stdout);
		ret = ipmi_on(i);

	} else if (!strcasecmp(op, "off")) {
		printf("Powering off machine @ IPMI:%s...", ip);
		fflush(stdout);
		ret = ipmi_off(i);
	} else if (!strcasecmp(op, "status")) {
		printf("Getting status of IPMI:%s...",ip);
		fflush(stdout);
		ret = ipmi_op(i, ST_STATUS, power_status);
		switch(ret) {
		case STATE_ON:
			printf("Chassis power = On\n");
			ret = 0;
			break;
		case STATE_OFF:
			printf("Chassis power = Off\n");
			ret = 0;
			break;
		default:
			printf("Chassis power = Unknown\n");
			ret = 1;
			break;
		}
	}


out:
	ipmi_destroy(i);
	free(i);
	if (ret == 0)
		printf("Done\n");
	else
		printf("Failed\n");
	return ret;
}
