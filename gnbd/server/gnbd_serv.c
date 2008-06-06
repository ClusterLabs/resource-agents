#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <getopt.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/poll.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/un.h>
#include <errno.h>
#include <syslog.h>

#include "gnbd_endian.h"
#include "gnbd_server.h"
#include "local_req.h"
#include "extern_req.h"
#include "gserv.h"
#include "trans.h"

#include "copyright.cf"

short unsigned int port = GNBD_SERVER_PORT;

#define BUFSIZE (sizeof(info_req_t) + sizeof(uint32_t))

struct connecter_s {
  int type;    /* LOCAL or EXTERNAL */
  uint32_t req; /* EXTERN_XXX or LOCAL_XXX, 0 for no request read yet */
  int size;
  char *buf;
};
typedef struct connecter_s connecter_t;

#define NORMAL_KILL 1
#define FORCE_KILL 2
int killing_gnbd_serv = 0;
connecter_t *connecters;
struct pollfd *polls;
int max_id;
int is_clustered = 1;

#define LOCAL 0
#define EXTERNAL 1

int usage(void){
  printf(
"Usage:\n"
"\n"
"gnbd_serv [options]\n"
"\n"
"Options:\n"
"  -h               print this help message\n"
"  -k               kill the currently running gnbd_serv\n"
"  -K               kill gnbd_serv even if there are exported devices\n"
"  -n               No cluster. Do not contact cluster manager\n"
"  -p <port>        port to start the gnbd_server on (default is 14567)\n"
"  -q               quiet mode.  Only print errors.\n"
"  -v               verbose output\n"
"  -V               version information\n");
  return 0;
}


void setup_poll(void)
{
  int i;

  polls = malloc(open_max() * sizeof(struct pollfd));
  if (!polls)
    fail_startup("cannot allocate poller structure : %s\n", strerror(errno));
  for (i = 0; i < open_max(); i++){
    polls[i].fd = -1;
    polls[i].revents = 0;
  }
  connecters = malloc(open_max() * sizeof(connecter_t));
  if (!connecters)
    fail_startup("cannot allocate connecter structure : %s\n",
                 strerror(errno));
  polls[LOCAL].fd = start_comm_device("gnbd_servcomm");
  if (polls[LOCAL].fd < 0)
    fail_startup("cannot startup local connection\n");
  polls[LOCAL].events = POLLIN;
  polls[EXTERNAL].fd = start_extern_socket(port);
  polls[EXTERNAL].events = POLLIN;
  max_id = 1;
}  

void add_poller(int fd, int type){
  int i;
  
  if (fd < 0)
    return;
  for (i = 0; polls[i].fd >= 0 && i < open_max(); i++);
  if (i >= open_max()){
    log_err("maximum number of open file descriptors reached\n");
    /* FIXME -- should I send a restart fail reply */
    close(fd);
    return;
  }
  connecters[i].buf = malloc(BUFSIZE);
  if (!connecters[i].buf){
    log_err("couldn't allocate memory for connection buffer\n");
    close(fd);
    return;
  }
  polls[i].fd  = fd;
  polls[i].events = POLLIN;
  connecters[i].type = type;
  connecters[i].req = 0;
  connecters[i].size = 0;
  if (i > max_id)
    max_id = i;
}

void remove_poller(int index)
{
  polls[index].fd = -1;
  polls[index].revents = 0;
  free(connecters[index].buf);
  while(polls[max_id].fd == -1)
    max_id--;
}

void close_poller(int index)
{
  close(polls[index].fd);
  if (index == LOCAL){
    polls[LOCAL].fd = start_comm_device("gnbd_servcomm");
    if (polls[LOCAL].fd < 0)
      /* FIXME -- should I try to recover from this */
      raise(SIGTERM);
  }
  else if (index == EXTERNAL)
    polls[EXTERNAL].fd = start_extern_socket(port);
  else
    remove_poller(index);
}

void handle_request(int index)
{
  int bytes;
  connecter_t *connecter = &connecters[index];
  int ret;

  errno = 0;
  bytes = read(polls[index].fd, connecter->buf + connecter->size,
               BUFSIZE - connecter->size);
  if (bytes <= 0){
    if (bytes == 0)
      log_err("unexpectedly read EOF on %s connection, index: %d, req: %d\n",
              (connecter->type == LOCAL)? "local" : "external",
              index, connecters->req);
    else if (errno != EINTR)
      log_err("cannot read from %s connection, index: %d, req: %d : %s\n",
              (connecter->type == LOCAL)? "local" : "external",
              index, connecter->req, strerror(errno));
    log_verbose("total read : %d bytes\n", connecter->size);
    close_poller(index);
    return;
  }
  
  connecter->size += bytes;
  if (connecter->req == 0 && connecter->size >= sizeof(uint32_t)){
    memcpy(&connecter->req, connecter->buf, sizeof(uint32_t));
    if (connecter->type == EXTERNAL)
      connecter->req = be32_to_cpu(connecter->req);
  }
  if (connecter->size < sizeof(uint32_t))
    return;
  if (connecter->type == LOCAL){
    ret = check_local_data_len(connecter->req,
                               connecter->size - sizeof(uint32_t));
    if (ret < 0)
      close_poller(index);
    else if (ret){
      handle_local_request(polls[index].fd, connecter->req,
                           connecter->buf + sizeof(uint32_t));
        remove_poller(index);
    }
    return;
  }
  else {
    ret = check_extern_data_len(connecter->req,
                                connecter->size - sizeof(uint32_t));
    if (ret < 0)
      close_poller(index);
    else if (ret) {
      handle_extern_request(polls[index].fd, connecter->req,
                            connecter->buf + sizeof(uint32_t));
        remove_poller(index);
    }
    return;
  }
}

void do_poll(void)
{
  int err;
  int i;

  /* FIXME --I should probably do something every timeout, like check the
     cluster manager */ 
  err = poll(polls, max_id + 1, -1);
  if (err <= 0){
    if (err < 0 && errno != EINTR)
      log_err("poll error : %s\n", strerror(errno));
    return;
  }
  for (i = 0; i <= max_id; i++){
    if (polls[i].revents & (POLLERR | POLLHUP | POLLNVAL)){
      log_err("Bad poll result, 0x%x on id %d\n", polls[i].revents, i);
      close_poller(i);
      continue;
    }
    if (polls[i].revents & POLLIN){
      if (i == LOCAL)
        add_poller(accept_local_connection(polls[i].fd), LOCAL);
      else if (i == EXTERNAL){
        int fd;
        
        fd = accept_extern_connection(polls[i].fd);
        add_poller(fd, EXTERNAL);
      }
      else
        handle_request(i);
    }
  }
}

  
void sig_term(int sig)
{
  exit(0);
}


void setup_signals(void)
{
  struct sigaction act;
  sigset_t block_sigchld_set;

  sigemptyset(&block_sigchld_set);
  sigaddset(&block_sigchld_set, SIGCHLD);

  memset(&act,0,sizeof(act));
  act.sa_handler = sig_term;
  act.sa_mask = block_sigchld_set;
  if( sigaction(SIGTERM, &act, NULL) <0)
    fail_startup("cannot setup SIGTERM handler : %s\n", strerror(errno));
  memset(&act,0,sizeof(act));
  act.sa_handler = sig_chld;
  if( sigaction(SIGCHLD, &act, NULL) <0)
    fail_startup("cannot setup SIGCHLD handler : %s\n", strerror(errno));
  memset(&act,0,sizeof(act));
  act.sa_handler = sig_hup;
  if( sigaction(SIGHUP, &act, NULL) <0)
    fail_startup("cannot setup SIGHUP handler : %s\n", strerror(errno));
}

void parse_cmdline(int argc, char **argv)
{
  int c;

  program_name = "gnbd_serv";
  while ((c = getopt(argc, argv, "hkKnp:qvV")) != -1){
    switch(c){
    case ':':
    case '?':
      fprintf(stderr, "Please use '-h' for usage.\n");
      exit(1);
      
    case 'h':
      usage();
      exit(0);

    case 'k':
      killing_gnbd_serv = NORMAL_KILL;
      continue;

    case 'K':
      killing_gnbd_serv = FORCE_KILL;
      continue;

    case 'n':
      is_clustered = 0;
      continue;

    case 'p':
      if (sscanf(optarg, "%hu", &port) != 1){
        printe("invalid port %s with -p\nPlease see man page for details.\n",
               optarg);
        exit(1);
      }
      continue;
      
    case 'q':
      if (verbosity == VERBOSE){
        printe("cannot use both -q and -v options\nPlease use '-h' for usage.\n");
        exit(1);
      }
      verbosity = QUIET;
      continue;

    case 'v':
      if (verbosity == QUIET){
        printe("cannot use both -q and -v options\nPlease use '-h' for usage.\n");
        exit(1);
      }
      verbosity = VERBOSE;
      continue;
      
    case 'V':
      printf("%s %s (built %s %s)\n", program_name, RELEASE_VERSION,
             __DATE__, __TIME__);
      printf("%s\n", REDHAT_COPYRIGHT);
      exit(0);

    default:
      printe("No action for option -- %c\nPlease use '-h' for usage.\n", c);
      exit(1);
    }
  }
}

void exit_main(void)
{
    if (!is_gserv) /* don't do this is you are a gserv process. Only
		      the main process should kill the gservs */
    	kill_all_gserv();
}

int main(int argc, char **argv)
{
  parse_cmdline(argc, argv);

  if (killing_gnbd_serv){
    int pid;
    if (!check_lock("gnbd_serv.pid", &pid)){
      printm("gnbd_serv not running\n");
      return 0;
    }
    if (killing_gnbd_serv == NORMAL_KILL){
      int fd;

      fd = connect_to_comm_device("gnbd_serv");
      if (fd < 0)
        return 1;
      if (send_cmd(fd, LOCAL_SHUTDOWN_REQ, "shutdown") < 0)
        return 1;
      if (recv_reply(fd, "shutdown") < 0)
        return 1;
      printm("gnbd_serv shutting down\n");
      return 0;
    }
    if (kill(pid, SIGTERM) < 0){
      printm("can't send gnbd_serv TERM signal : %s\n", strerror(errno));
      return 1;
    }
    printm("gnbd_serv killed\n");
    return 0;
  }

  daemonize_and_exit_parent();

  if(!pid_lock(""))
    fail_startup("%s already running\n", program_name);

  setup_signals();
  /* FIXME -- somewhere I should set myself to nobody */

  if (get_my_nodename(nodename, is_clustered) < 0){
    if (is_clustered){
      printe("cannot get node name : %s\n", strerror(errno));
      if (errno == ESRCH)
        printe("No cluster manager is running\n");
      else if (errno == ELIBACC)
        printe("cannot find magma plugins\n");
      fail_startup("If you are not planning to use a cluster manager, use -n\n");
    }
    else 
      fail_startup("cannot get node name : %s\n", strerror(errno));
  }

  setup_poll();

  if (atexit(exit_main) < 0)
    fail_startup("cannot register function with atexit\n");
  
  finish_startup("startup succeeded\n");

  while(1){
    do_poll();
  }

  exit(0);
}
