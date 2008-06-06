/** @file
 * Fencing agent for WTI RPS-10 (serial) power devices.  Based on
 * the fence_apc agent from linux-cluster, and the prb utility (which
 * controls RPS-10s and PRB-5 rev 1 remote power switches).
 *
 * Only works in 2-node clusters because of the requirement that each node
 * be able to fence each other node.  This driver does not support using the
 * 'all ports' directive; cluster machines with multiple power supplies will
 * need to have their configuration updated accordingly if they are upgrading
 * from clumanager 1.0.x or 1.2.x.
 */
#include <stdio.h>
#include <termios.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/select.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <sys/ioctl.h>
#include <libgen.h>
#include "copyright.cf"

#ifndef RELEASE_VERSION
#define RELEASE_VERSION "sandbox"
#endif


/*
 * Salt to taste.
 */
#define DEFAULT_DEVICE "/dev/ttyS0"
#define DEFAULT_SPEED  B9600
#define RPS10_CMD_STR   "\x02\x18\x18\x02\x18\x18%d%c\r"


/**
  Open a serial port and lock it.
 */
int
open_port(char *file, int speed)
{
	struct termios  ti;
	int fd;
	struct flock lock;

	if ((fd = open(file, O_RDWR | O_EXCL)) == -1) {
		perror("open");
		return -1;
	}

	memset(&lock,0,sizeof(lock));
	lock.l_type = F_WRLCK;
	if (fcntl(fd, F_SETLK, &lock) == -1) {
		perror("Failed to lock serial port");
		close(fd);
		return -1;
	}
		
	memset(&ti, 0, sizeof(ti));
	ti.c_cflag = (speed | CLOCAL | CRTSCTS | CREAD | CS8);

	if (tcsetattr(fd, TCSANOW, &ti) < 0) {
		perror("tcsetattr");
		close(fd);
		return -1;
	}                             

	tcflush(fd, TCIOFLUSH);

	return fd;
}


/**
  Toggle data terminal ready (basically, hangup).  This will cause the RPS-10
  to print a "RPS-10 Ready" message.
 */
void
hangup(int fd, int delay)
{
	unsigned int bits;

	if (ioctl(fd, TIOCMGET, &bits)) {
		perror("ioctl1");
		return;
	}

	bits &= ~(TIOCM_DTR | TIOCM_CTS | TIOCM_RTS | TIOCM_DSR | TIOCM_CD);

	if (ioctl(fd, TIOCMSET, &bits)) {
		perror("ioctl2");
		return;
	}
	
	usleep(delay);

	bits |= (TIOCM_DTR | TIOCM_CTS | TIOCM_RTS | TIOCM_DSR | TIOCM_CD);

	if (ioctl(fd, TIOCMSET, &bits)) {
		perror("ioctl3");
		return;
	}
}


int
char_to_speed(char *speed)
{
	if (!strcmp(speed, "300"))
		return B300;
	if (!strcmp(speed, "1200"))
		return B1200;
	if (!strcmp(speed, "2400"))
		return B2400;
	if (!strcmp(speed, "9600"))
		return B9600;
	return -1;
}


void
usage_exit(char *pname)
{
printf("usage: %s <options>\n", pname);
printf("   -n <#>         Specify RPS-10 port number <#>.  Default=0\n"
       "                  Valid ports: 0-9\n");
printf("   -d <device>    Use serial device <dev>.  Default=%s\n",
       DEFAULT_DEVICE);
printf("   -s <speed>     Use speed <speed>. Default=9600\n"
       "                  Valid speeds: 300, 1200, 2400, 9600\n");
printf("   -o <op>        Operation to perform.\n");
printf("                  Valid operations: on, off, [reboot]\n");
printf("   -V             Print version and exit\n");
printf("   -v             Verbose mode\n\n");
printf("If no options are specified, the following options will be read\n");
printf("from standard input (one per line):\n\n");
printf("   port=<#>       Same as -n\n");
printf("   device=<dev>   Same as -d\n");
printf("   speed=<speed>  Same as -s\n");
printf("   option=<op>    Same as -o\n");
printf("   operation=<op> Same as -o\n");
printf("   action=<op>    Same as -o\n");
printf("   verbose        Same as -v\n\n");
	exit(1);
}


/**
  Perform an operation on an RPS-10.
 */
int
rps10_port_op(int fd, int port, char cmd)
{
	char buf[30];

	snprintf(buf, sizeof(buf), RPS10_CMD_STR, port, cmd);
	if (write(fd, buf, strlen(buf)) != strlen(buf))
		return -1;

	return 0;
}


/**
  Toggle = ^B^X^X^B^X^X<port>T^M
 */
int
rps10_toggle_port(int fd, int port)
{
	return rps10_port_op(fd, port, 'T');
}


/**
  Power-off = ^B^X^X^B^X^X<port>0^M
 */
int
rps10_port_off(int fd, int port)
{
	return rps10_port_op(fd, port, '0');
}


/**
  Power-on = ^B^X^X^B^X^X<port>1^M
 */
int
rps10_port_on(int fd, int port)
{
	return rps10_port_op(fd, port, '1');
}


/**
  Super-simple expect code.
 */
int
wait_for(int fd, char *what, int timeout)
{
	char *wp, *wend, c;
	struct timeval tv;
	fd_set rfds;
	int l;
       
	if (!what)
		return -1;

	l = strlen(what);

	if (!l)
		return -1;

	tv.tv_sec = timeout;
	tv.tv_usec = 0;

	wp = what;
	wend = what + l;

	while (wp != wend) {
		FD_ZERO(&rfds);
		FD_SET(fd, &rfds);

		switch(select(fd+1, &rfds, NULL, NULL, &tv)) {
		case -1:
			return -1;
		case 0:
			errno = ETIMEDOUT;
			return -1;
		}

		if (read(fd, &c, 1) == -1)
			return -1;

		if (*wp == c)
			wp++;
		else
			wp = what;
	}

	return 0;
}


/**
   Remove leading and trailing whitespace from a line of text.
 */
int
cleanup(char *line, size_t linelen)
{
	char *p;
	int x;

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
get_options_stdin(char *dev, size_t devlen, int *speed, int *port,
		  char *op, size_t oplen, int *verbose)
{
	char in[256];
	int line = 0;
	char *name, *val;

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
		} else if (!strcasecmp(name, "device")) {
			/* Character device to use.  E.g. /dev/ttyS0 */
			if (val)
				strncpy(dev, val, devlen);
			else
				dev[0] = 0;

		} else if (!strcasecmp(name, "port")) {
			/* Port number */
			if (val)
				*port = atoi(val);
			else
				*port = -1;

		} else if (!strcasecmp(name, "speed")) {
			/* Speed in bits per second */
			if (val)
				*speed = char_to_speed(val);
			else
				*speed = -1;
		} else if (!strcasecmp(name, "option") ||
			   !strcasecmp(name, "operation") ||
			   !strcasecmp(name, "action")) {
			if (val)
				strncpy(op, val, oplen);
			else
				op[0] = 0;
		} else {
			fprintf(stderr,
				"parse error: illegal name on line %d\n",
				line);
			return 1;
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


int
main(int argc, char **argv)
{
	int fd, speed = DEFAULT_SPEED, opt, ret = 1;
	extern char *optarg;
	char dev[256];
	char op[256];
	int port = 0, verbose=0;
	char *pname = basename(argv[0]);

	strncpy(dev, DEFAULT_DEVICE, sizeof(dev));
	strncpy(op, "reboot", sizeof(op));

	if (argc > 1) {
		/*
		   Parse command line options if any were specified
		 */
		while ((opt = getopt(argc, argv, "s:d:n:ro:vV?hH")) != EOF) {
			switch(opt) {
			case 's':
				/* Speed */
				speed = char_to_speed(optarg);
				if (speed == -1)
					usage_exit(pname);
	
				break;
			case 'd':
				/* Device to open */
				strncpy(dev, optarg, sizeof(dev));
				break;
			case 'n':
				port = atoi(optarg);
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
		if (get_options_stdin(dev, sizeof(dev), &speed, &port,
				      op, sizeof(op), &verbose) != 0)
			return 1;
	}

	/*
	   Validate the operating parameters
	 */
	if (strlen(dev) == 0)
		fail_exit("no device specified");

	if (speed == -1)
		fail_exit("invalid serial port speed");

	if (strcasecmp(op, "off") && strcasecmp(op, "on") &&
	    strcasecmp(op, "reboot")) {
		fail_exit("operation must be 'on', 'off', or 'reboot'");
	}

	if ((port < 0) && (port != 9))
		fail_exit("port must be between 0 and 9, inclusive");

	/*
	   Open the serial port up
	 */
	fd = open_port(dev, speed);
	if (fd == -1)
		exit(1);

	if (verbose) {
		printf("Toggling DTR...");
		fflush(stdout);
	}
	hangup(fd, 500000);
	if (verbose)
		printf("Done\n");

	/*
	   Some misc. RPS-10s return PRS for some reason...
	 */
	if (verbose) {
		printf("Waiting for Ready signal...");
		fflush(stdout);
	}
	if (wait_for(fd, "S-10 Ready", 10) == -1) {
		perror("wait_for");
		return -1;
	}
	if (verbose)
		printf("Done\n");

	/*
	   Perform the requested operation
	 */
	if (!strcasecmp(op, "reboot")) {
		printf("Rebooting port %d...", port);
		fflush(stdout);
		if (rps10_toggle_port(fd, port) < 0)
			goto out;

		if (wait_for(fd, " Off", 10) < 0)
			goto out;

		/* turning on doesn't require a failure check */
		if (wait_for(fd, " On", 10) != 0)
			printf("<warn: "
			       "Plug %d might still be off>", port);

		ret = 0;

	} else if (!strcasecmp(op, "on")) {
		printf("Powering on port %d...", port);
		fflush(stdout);
		if (rps10_port_on(fd, port) < 0)
			goto out;

		if (wait_for(fd, " On", 10) < 0)
	       		goto out;

		ret = 0;

	} else if (!strcasecmp(op, "off")) {
		printf("Powering off port %d...", port);
		fflush(stdout);
		if (rps10_port_off(fd, port) < 0)
			goto out;

		if (wait_for(fd, " Off", 10) < 0)
			goto out;

		ret = 0;
	}

out:
	if (ret == 0)
		printf("Done\n");
	else
		printf("Failed\n");
	return ret;
}
