/******************************************************************************
*******************************************************************************
**
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
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "log.h"
#include "debug.h"
#include "cnx_mgr.h"
#include "cman_mgr.h"

#include "copyright.cf"

static unsigned int flags=0;
#define FLAG_MULTICAST	1
#define FLAG_NODAEMON	2
#define FLAG_VERBOSE	4

static char *config_file_location = NULL;
#define DEFAULT_CONFIG_LOCATION "/etc/cluster/cluster.xml"
static char *lockfile_location = NULL;
#define DEFAULT_CCSD_LOCKFILE "/var/run/sistina/ccsd.pid"

int fe_port = 50006;
int be_port = 50007;

static void parse_cli_args(int argc, char *argv[]);
static void daemonize(void);
static void print_start_msg(void);


int main(int argc, char *argv[]){
  int i,error=0;
  int sfds[2], afd;
  struct sockaddr_in addr;
  fd_set rset, tmp_set;

  parse_cli_args(argc, argv);

  daemonize();

  print_start_msg();

  if(start_cman_monitor_thread()){
    log_err("Unable to create thread.\n");
    exit(EXIT_FAILURE);
  }

  /** Setup the socket to communicate with the CCS library **/
  sfds[0] = socket(PF_INET, SOCK_STREAM, 0);
  if(sfds[0] < 0){
    log_sys_err("Socket creation failed");
    return -errno;
  } else {
    int trueint = 1;
    setsockopt(sfds[0], SOL_SOCKET, SO_REUSEADDR, &trueint, sizeof(int));
  }

  addr.sin_family = AF_INET;
  inet_aton("127.0.0.1", (struct in_addr *)&addr.sin_addr.s_addr);
  addr.sin_port = htons(fe_port);
 
  if(bind(sfds[0], (struct sockaddr *)&addr, sizeof(struct sockaddr_in)) < 0){
    log_sys_err("Unable to bind socket");
    close(sfds[0]);
    return -errno;
  }
 
  listen(sfds[0], 5);


  /** Setup the socket to communicate with the CCS library **/
  sfds[1] = socket(PF_INET, SOCK_DGRAM, 0);
  if(sfds[1] < 0){
    log_sys_err("Socket creation failed");
    return -errno;
  } else {
    int trueint = 1;
    setsockopt(sfds[1], SOL_SOCKET, SO_REUSEADDR, &trueint, sizeof(int));
  }

  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = INADDR_ANY;
  addr.sin_port = htons(be_port);
 
  if(bind(sfds[1], (struct sockaddr *)&addr, sizeof(struct sockaddr_in)) < 0){
    log_sys_err("Unable to bind socket");
    close(sfds[1]);
    return -errno;
  }
 
  listen(sfds[1], 5);

  FD_ZERO(&rset);
  FD_SET(sfds[0], &rset);
  FD_SET(sfds[1], &rset);

  while(1){
    int len = sizeof(struct sockaddr_in);
    
    tmp_set = rset;

    select(FD_SETSIZE, &tmp_set, NULL,NULL,NULL);
    
    for(i=0; i<2; i++){
      if(!FD_ISSET(sfds[i], &tmp_set)){
	continue;
      }
      if(i == 0){
	afd = accept(sfds[i], (struct sockaddr *)&addr, &len);
	if(afd < 0){
	  log_sys_err("Unable to accept connection");
	  continue;
	}

	if(ntohs(addr.sin_port) > 1024){
	  log_err("Refusing connection from port > 1024:  port = %d",
		  ntohs(addr.sin_port));
	  close(afd);
	  continue;
	}
	if((error = process_request(afd))){
	  log_err("Error while processing request: %s\n", strerror(-error));
	}
	close(afd);
      } else {
	if((error = process_broadcast(sfds[i]))){
	  log_err("Error while processing broadcast: %s\n", strerror(-error));
	}
      }
    }
  }
  exit(EXIT_SUCCESS);
}


/**
 * print_usage - print usage information
 * @stream: open file stream to print to
 *
 */
static void print_usage(FILE *stream){
  ENTER("print_usage");
  fprintf(stream,
	  "Usage:\n"
	  "\n"
	  "ccsd [Options]\n"
	  "\n"
	  "Options:\n"
	  " -h          Help.\n"
	  " -m          Use multicast instead of broadcast.\n"
	  " -n          No Daemon.  Run in the foreground.\n"
	  /*	  " -p <file>   Specify the location of the pid file.\n"*/
	  " -V          Print version information.\n"
	  " -v          Verbose.\n"
	  );
  EXIT("print_usage");
}


/**
 * parse_cli_args
 * @argc:
 * @argv:
 *
 * This function parses the command line arguments and sets the
 * appropriate flags in the global 'flags' variable.  Additionally,
 * it sets the global 'config_file_location'.  This function
 * will either succeed or cause the program to exit.
 */
static void parse_cli_args(int argc, char *argv[]){
  int c, error=0;

  ENTER("parse_cli_args");

  config_file_location = strdup(DEFAULT_CONFIG_LOCATION);
  lockfile_location = strdup(DEFAULT_CCSD_LOCKFILE);
  if(!config_file_location || !lockfile_location){
    fprintf(stderr, "Insufficient memory.\n");
    error = -ENOMEM;
    goto fail;
  }

  while((c = getopt(argc, argv, "cdf:hlmnsVv")) != -1){
    switch(c){
    case 'c':
      fprintf(stderr, "The '-c' option is depricated.\n"
	      "Try '-h' for help.\n");
      error = -EINVAL;
      goto fail;
    case 'd':  /* might be usable for upgrade */
      fprintf(stderr, "The '-d' option is depricated.\n"
	      "Try '-h' for help.\n");
      error = -EINVAL;
      goto fail;
    case 'f':  /* might be usable for upgrade */
      free(config_file_location);
      config_file_location = optarg;
      break;
    case 'h':
      print_usage(stdout);
      exit(EXIT_SUCCESS);
    case 'l':
      fprintf(stderr, "The '-l' option is depricated.\n"
	      "Try '-h' for help.\n");
      error = -EINVAL;
      goto fail;
    case 'm':
      flags |= FLAG_MULTICAST;
      break;
    case 'n':
      flags |= FLAG_NODAEMON;
      break;
    case 'p':
      free(lockfile_location);
      lockfile_location = optarg;
    case 's':
      fprintf(stderr, "The '-s' option is depricated.\n"
	      "Try '-h' for help.\n");
      error = -EINVAL;
      goto fail;
    case 'V':
      printf("%s %s (built %s %s)\n", argv[0], CCS_RELEASE_NAME, __DATE__, __TIME__);
      printf("%s\n", REDHAT_COPYRIGHT);
      exit(EXIT_SUCCESS);
    case 'v':
      flags |= FLAG_VERBOSE;
      break;
    default:
      print_usage(stderr);
      error = -EINVAL;
      goto fail;
    }
  }

 fail:
  EXIT("parse_cli_args");

  if(error){
    exit(EXIT_FAILURE);
  }
}


/**
 * create_lockfile - create and lock a lock file
 * @lockfile: location of lock file
 *
 * Returns: 0 on success, -1 otherwise
 */
static int create_lockfile(char *lockfile){
  int fd, error=0;
  struct stat stat_buf;
  struct flock lock;
  char buffer[50];

  ENTER("create_lockfile");
   
  if(!strncmp(lockfile, "/var/run/sistina/", 17)){
    if(stat("/var/run/sistina", &stat_buf)){
      if(mkdir("/var/run/sistina", S_IRWXU)){
        log_sys_err("Cannot create lockfile directory");
        error = -errno;
	goto fail;
      }
    } else if(!S_ISDIR(stat_buf.st_mode)){
      log_err("/var/run/sistina is not a directory.\n"
              "Cannot create lockfile.\n");
      error = -ENOTDIR;
      goto fail;
    }
  }
 
  if((fd = open(lockfile, O_CREAT | O_WRONLY,
                (S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH))) < 0){
    log_sys_err("Cannot create lockfile");
    error = -errno;
    goto fail;
  }
 
  lock.l_type = F_WRLCK;
  lock.l_start = 0;
  lock.l_whence = SEEK_SET;
  lock.l_len = 0;
 
  if (fcntl(fd, F_SETLK, &lock) < 0) {
    close(fd);
    log_err("The ccsd process is already running.\n");
    error = -errno;
    goto fail;
  }
 
  if (ftruncate(fd, 0) < 0) {
    close(fd);
    error = -errno;
    goto fail;
  }
 
  sprintf(buffer, "%d\n", getpid());
 
  if(write(fd, buffer, strlen(buffer)) < strlen(buffer)){
    close(fd);
    unlink(lockfile);
    error = -errno;
    goto fail;
  }

 fail: 
  EXIT("create_lockfile");
  
  /* leave fd open - rely on exit to close it */
  if(error){
    return error;
  } else {
    return 0;
  }
}


/**
 * parent_exit_handler: exit the parent
 * @sig: the signal
 *
 * This function allows us to send the parent process any
 * failure notices that might occur in the child when daemonizing.
 * This way, the user can be notified by the proper process.
 */
static void parent_exit_handler(int sig){
  int error;

  ENTER("parent_exit_handler");

  switch(sig){
  case SIGUSR1:
    fprintf(stderr, "Failed to create lock file.\n");
    error = EXIT_FAILURE;
    break;
  default:
    error = EXIT_SUCCESS;
  }

  EXIT("parent_exit_handler");
  exit(error);
}


/**
 * sig_handler
 * @sig
 *
 * This handles signals which the daemon might receive.
 */
static void sig_handler(int sig){
  int err;

  ENTER("sig_handler");

  switch(sig){
  case SIGINT:
    log_msg("Stopping ccsd, SIGINT received.\n");
    err = EXIT_SUCCESS;
    break;
  case SIGQUIT:
    log_msg("Stopping ccsd, SIGQUIT received.\n");
    err = EXIT_SUCCESS;
    break;
  case SIGTERM:
    log_msg("Stopping ccsd, SIGTERM received.\n");
    err = EXIT_SUCCESS;
    break;
  case SIGSEGV:
    log_err("Stopping ccsd, SIGSEGV received.\n");
    err = EXIT_FAILURE;
    break;
  default:
    log_err("Stopping ccsd, unknown signal received.\n");
    err = EXIT_FAILURE;
  }

  EXIT("sig_handler");
  exit(err);
}


/**
 * daemonize
 *
 * This function will do the following:
 * - daemonize, if required
 * - set up the lockfile
 * - set up logging
 * - set up signal handlers
 * It will cause the program to exit if there is a failure.
 */
static void daemonize(void){
  int error=0;
  int pid;

  ENTER("daemonize");

  if(flags & FLAG_NODAEMON){
    log_dbg("Entering non-daemon mode.\n");
    if((error = create_lockfile(lockfile_location))){
      goto fail;
    }
    signal(SIGINT, &sig_handler);
    signal(SIGQUIT, &sig_handler);
    signal(SIGTERM, &sig_handler);
    signal(SIGSEGV, &sig_handler);
  } else {
    log_dbg("Entering daemon mode.\n");

    signal(SIGTERM, &parent_exit_handler);
    signal(SIGUSR1, &parent_exit_handler);

    pid = fork();

    if(pid < 0){
      fprintf(stderr, "Unable to fork().\n");
      error = pid;
      goto fail;
    }

    if(pid){
      while(1){  /* parent awaits signal from child */
	sleep(5);
      }
    }

    setsid();
    chdir("/");
    umask(0);

    close(0); close(1); close(2);
    open("/dev/null", O_RDONLY); /* reopen stdin */
    open("/dev/null", O_WRONLY); /* reopen stdout */
    open("/dev/null", O_WRONLY); /* reopen stderr */

    log_open("ccsd", LOG_PID, LOG_DAEMON);
  
    if((error = create_lockfile(lockfile_location))){
      kill(getppid(), SIGUSR1);
      goto fail;
    }

    kill(getppid(), SIGTERM);
    signal(SIGINT, &sig_handler);
    signal(SIGQUIT, &sig_handler);
    signal(SIGTERM, &sig_handler);
    signal(SIGSEGV, &sig_handler);
  }

 fail:
  EXIT("daemonize");

  if(error){
    exit(EXIT_FAILURE);
  }
}


/**
 * print_start_msg
 *
 */
static void print_start_msg(void){
  log_msg("Starting ccsd %s:\n", CCS_RELEASE_NAME);
  log_msg(" Built: "__DATE__" "__TIME__"\n");
  log_msg(" %s\n", REDHAT_COPYRIGHT);
}
