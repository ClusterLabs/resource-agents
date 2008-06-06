/** @file
 * This program simply parses command line args and then calls the
 * corresponding system call.
 * These operations are for NFS failover semantics in an attempt to
 * shield clients from unnecessary stale file handle errors.
 *
 * File Origin - this command was reverse-engineered by starting with the
 * GPL syscall interfaces defined in <linux/nfsd/syscall.h>.
 */
 
#ifdef X86_64
#define __ASM_SYSTEM_H
#define __ASM_X86_64_PROCESSOR_H
#endif

 
#define _LVM_H_INCLUDE /* XXX */
#include <linux/list.h>
#include <linux/kdev_t.h>
#undef _LVM_H_INCLUDE
#include <linux/socket.h>
#include <linux/types.h>

#ifdef S390
#define __ssize_t_defined
#endif

#include <linux/unistd.h>
/* 
 * lhh - someone thought it would be cute to remove the #ifdef __KERNEL__
 * around some of the system includes.  Now we have to fudge the #inclusion
 * of linux/types.h _AS_ sys/types.h just to compile.
 */
#define _SYS_TYPES_H
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/errno.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/param.h>
#include <signal.h>
#include <syslog.h>

/* XXX - should be this file, 
#include <linux/nfsd/syscall.h>
 */
/*
 * But, temporarily include this file to simplify the build on 
 * systems which aren't yet installed and therefore don't have the
 * header file w/ updated defines for new nfs syscalls.
 */
#include "syscall.h"

/*
 * Cluster include
 */
#include <clulog.h>


/* Forward routine declarations. */
static void usage(const char * cmd);
int validateDevice(char *name);
int nfsctl(int cmd, struct nfsctl_arg * argp, void * resp);

/* Program Globals */
static int verbose = 0;
static char *cmdname;

static void
usage(const char * cmd)
{
        fprintf(stderr, "\n");
        fprintf(stderr, "usage: %s [-ehsv] "
		        "[-d deviceName] "
		        "\n", cmd);
        fprintf(stderr, "Options:\n");
        fprintf(stderr, "-e\t\tInitiate flush of pending requests.\n");
        fprintf(stderr, "-h\t\tPrints this help message.\n");
        fprintf(stderr, "-s\t\tCancel pending flush of pending requests.\n");
        fprintf(stderr, "-v\t\tIncreases verbose debugging level.\n");
        fprintf(stderr, "\n");
        fprintf(stderr, "NOTE: this command will only successfully run\n");
        fprintf(stderr, "      if the correspoinding NFS kernel patches\n");
        fprintf(stderr, "      are built into the kernel.\n");
        fprintf(stderr, "\n");
										        exit(1);
}

int
main(int argc, char** argv)
{
	extern char *	optarg;
	extern int	optind, opterr, optopt;
	int errors = 0;
	int errno_save = 0;
	int nfsSyscallNum = 0;
	char *nfsSyscallString = "BOGUS";
	char *deviceName = NULL;
	int c;
	struct nfsctl_arg nfsctlarg;
	int retval;

    	if (geteuid() != (uid_t)0) {
          	clulog_and_print(LOG_ERR, "%s must be run as the root user.\n",
                	argv[0]);
        	exit(1);    
    	}
	if ((cmdname = strrchr(argv[0], '/')) == NULL) {
		cmdname = argv[0];
	}
	else{
		++cmdname;
	}

	while ((c = getopt(argc, argv, "d:ehsv")) != -1) {
		switch(c) {
		case 'd':	deviceName = optarg;
				break;
		case 'e':	
				nfsSyscallNum = NFSCTL_FODROP;
				nfsSyscallString = "NFSCTL_FODROP";
				break;
		case 'h':	usage(cmdname);
				break;
		case 's':	
				nfsSyscallNum = NFSCTL_STOPFODROP;
				nfsSyscallString = "NFSCTL_STOPFODROP";
				break;
		case 'v':	++verbose;
				break;
		default:	++errors;
				break;
		}
	}
	if (nfsSyscallNum == 0) {
		clulog_and_print(LOG_ERR, "%s: No NFS syscall has been "
				 "specified.\n",cmdname);
		++errors;
	}
	if (deviceName == NULL) {
		clulog_and_print(LOG_ERR, "%s: No device name has been "
				 "specified.\n", cmdname);
		++errors;
	}
	if (validateDevice(deviceName)) {
		++errors;
	}
	if (errors) {
		usage(cmdname);
	}
	if (verbose) {
		printf("Calling: nfsSyscall = %s, deviceName = %s.\n", 
				nfsSyscallString, deviceName);
	}
	/*
	 * Setup the data structure expected by the kernel.
	 */
	memset(&nfsctlarg, 0, sizeof(struct nfsctl_arg));
	nfsctlarg.ca_version = NFSCTL_VERSION;
	strncpy(nfsctlarg.ca_fodrop.fo_dev, deviceName,
	       sizeof(nfsctlarg.ca_fodrop.fo_dev));
	nfsctlarg.ca_fodrop.fo_timeout = 1000; /* range 600-1200 */
	/*
	 * Jump into the kernel syscall.
	 */
	retval = nfsctl(nfsSyscallNum, &nfsctlarg, NULL);
	if (retval != 0) {
		/* clulog_and_print calls syslog(), which modifies errno */
		errno_save = errno;
		clulog_and_print(LOG_WARNING, "#72: %s: NFS syscall %s failed: "
				 "%s.\n", cmdname, nfsSyscallString, 
				 strerror(errno_save));
		if (errno_save == EINVAL) {
			clulog_and_print(LOG_WARNING,
				         "#73: %s: Kernel may not "
					 "have NFS failover enhancements.\n",
					 cmdname);
		}
		exit(errno_save);
	}
	else {
	    if (verbose) {
		printf("SUCCESS: nfsSyscall = %s, deviceName = %s.\n", 
				nfsSyscallString, deviceName);
	    }
	}
	exit(0);
}

/*
 * Validate the device parameter.  Make sure it refers to a block
 * device special file.
 * Returns: 0=success, nonzero=error.
 */
int
validateDevice(char *name) {
	struct stat stat_st, *stat_ptr;
	stat_ptr = &stat_st;

	if (stat(name, stat_ptr) < 0) {
		clulog_and_print(LOG_ERR, "%s: Unable to stat %s.\n", cmdname, name);
		return(1);
	}
	/*
	 * Verify that its a block or character special file.
	 */
	if (S_ISBLK(stat_st.st_mode) == 0) {
		clulog_and_print(LOG_ERR, "%s: %s is not a block special file.\n", cmdname, name);
		return(1);
	}
	return(0); // success
}

/*
 * Routine to format make appropriate NFS syscall.
 * Leveraged from nfs-utils.
 */
int
nfsctl(int cmd, struct nfsctl_arg * argp, void * resp)
{
	  return syscall (__NR_nfsservctl, cmd, argp, resp);
}

