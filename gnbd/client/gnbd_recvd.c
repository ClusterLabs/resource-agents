#include <unistd.h>
#include <stdlib.h>
#include <getopt.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include <inttypes.h>

#include "gnbd.h"
#include "gnbd_endian.h"
#include "gnbd_utils.h"
#include "gserv.h"
#include "trans.h"
#include "extern_req.h"

#include "copyright.cf"

unsigned int minor_num;
int devfd;
uint64_t timestamp;
int daemon_mode = 0;
int forget_mode = 0;
int have_connected = 0;
char devname[32];
char node_name[65];
int is_clustered = 1;

#define fail(fmt, args...) \
do { \
  if (daemon_mode && !have_connected) \
    fail_startup(fmt, ##args); \
  if (daemon_mode){ \
    log_err(fmt, ##args); \
    exit(1); \
  } \
  print_err(fmt, ##args); \
  exit(1); \
} while(0)

#define tell_msg(fmt, args...) \
do { \
  if (daemon_mode) \
    log_msg(fmt, ##args); \
  if (!daemon_mode || !have_connected) \
    printm(fmt, ##args); \
} while(0)

#define tell_err(fmt, args...) \
do { \
  if (daemon_mode) \
    log_err(fmt, ##args); \
  if (!daemon_mode || !have_connected) \
    print_err(fmt, ##args); \
} while(0)

#define tell_fail(fmt, args...) \
do { \
  if (daemon_mode) \
    log_fail(fmt, ##args); \
  if (!daemon_mode || !have_connected) \
    printe(fmt, ##args); \
} while(0)

int usage(void){
  printf(
"Usage:\n"
"\n"
"gnbd_recvd [options]\n"
"gnbd_recvd [options] <minor_number>\n"
"\n"
"Options:\n"
"  -d               daemon mode. Print to syslog, not stdout/err\n"
"  -f               forget mode, implies daemon mode. Return immediately.\n"
"                   Don't wait for gnbd_recvd to connect to the server\n"
"  -h               print this help message\n"
"  -n               No cluster. Do not contact cluster manager\n"
"  -q               quiet mode. Only print errors.\n"
"  -v               verbose output\n"
"  -V               version information\n");
  return 0;
}
    
void parse_cmdline(int argc, char **argv)
{
  int c;
  struct stat stat_buf;
  program_name = "gnbd_recvd";
  char sysfs_base[25];

  while((c = getopt(argc, argv, "dfhnqvV")) != -1){
    switch(c){
    case ':':
    case '?':
      fprintf(stderr, "Please use '-h' for usage.\n");
      exit(1);
    case 'd':
      daemon_mode = 1;
      continue;
    case 'f':
      daemon_mode = 1;
      forget_mode = 0;
      continue;
    case 'h':
      usage();
      exit(0);
    case 'n':
      is_clustered = 0;
      continue;
    case 'q':
      if (verbosity == VERBOSE){
        printe("cannot use both -q and -v options\n"
               "Please use '-h' for usage.\n");
        exit(1);
      }
      verbosity = QUIET;
      continue;
    case 'v':
      if (verbosity == QUIET){
        printe("cannot use both -q and -v options\n"
               "Please use '-h' for usage.\n");
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
      printe("No action for option -- %c\n"
             "Please use '-h' for usage.\n", c);
      exit(1);
    }
  }
  if (optind == argc){
    printe("No minor number specified.\n"
           "Please use '-h' for usage.\n");
    exit(1);
  }
  if (optind < argc - 1){
    printe("Extra arguments after minor number.\n"
           "Pleae use '-h' for usage.\n");
    exit(1);
  }
  if (sscanf(argv[optind], "%u", &minor_num) != 1 || minor_num >= MAX_GNBD){
    printe("%s is not a valid minor number.\n"
           "Please see the man page for details\n", argv[optind]);
    exit(1);
  }
  snprintf(sysfs_base, 25, "/sys/class/gnbd/gnbd%u", minor_num);
  if (stat(sysfs_base, &stat_buf) < 0){
    if (errno == ENOENT || errno == ENOTDIR)
      printe("cannot stat %s : %s\n"
             "Check that the gnbd module is loaded and sysfs is mounted\n",
             sysfs_base, strerror(errno));
    else
      printe("cannot stat %s : %s\n", sysfs_base, strerror(errno));
    exit(1);
  }
  if (!(S_ISDIR(stat_buf.st_mode))){
    printe("%s is not a directory\n", sysfs_base);
    exit(1);
  }
}

void get_devname(void)
{
  if (do_get_sysfs_attr(minor_num, "name") == NULL)
    fail("cannot get /sys/class/gnbd/gnbd%d/name value : %s\n", minor_num,
         strerror(errno));
  strncpy(devname, sysfs_buf, 32);
}

void open_device(void)
{
  if( (devfd = open("/dev/gnbd_ctl", O_RDONLY)) < 0)
    fail("cannot open gnbd control device : %s\n", strerror(errno));
  if (ioctl(devfd, GNBD_GET_TIME, &timestamp) < 0)
    fail("cannot get timestamp : %s\n", strerror(errno));
}

int kill_old_gservs(char *host, unsigned short port)
{
  int sock;
  device_req_t device;
  node_req_t node;
  uint32_t cmd = EXTERN_KILL_GSERV_REQ;

  strncpy(device.name, devname, 32);
  strncpy(node.node_name, node_name, 65);

  if( (sock = connect_to_server(host, port)) < 0){
    tell_err("cannot connect to server %s (%d) : %s\n", host, sock,
           strerror(errno));
    return -1;
  }
  if (send_u32(sock, cmd) < 0){
    tell_err("cannot send kill server request to %s : %s\n", host,
             strerror(errno));
    goto fail;
  }
  if (retry_write(sock, &device, sizeof(device)) < 0){
    tell_err("transfer of device name to %s failed : %s\n", host,
             strerror(errno));
    goto fail;
  }
  if (retry_write(sock, &node, sizeof(node)) < 0){
    tell_err("transfer of my node name to %s failed : %s\n", host,
             strerror(errno));
    goto fail;
  }
  if (recv_u32(sock, &cmd) < 0){
    tell_err("reading kill server reply from %s failed : %s\n", host,
             strerror(errno));
    goto fail;
  }
  if (cmd && cmd != ENODEV){
    tell_err("kill server request from %s failed : %s\n", host,
             strerror(errno));
    goto fail;
  }
  close(sock);
  return 0;

 fail:
  close(sock);
  return -1;
}


/* If communication fails, retry.  If I receive a failure reply, just exit */
int gnbd_login(int sock, char *host)
{
  login_req_t login_req;
  login_reply_t login_reply;
  node_req_t node;
  uint32_t cmd = EXTERN_LOGIN_REQ;

  strncpy(node.node_name, node_name, 65);

  if (send_u32(sock, cmd) < 0){
    tell_err("cannot send login request to %s : %s\n", host, strerror(errno));
    return -1;
  }

  login_req.timestamp = timestamp;
  login_req.version = PROTOCOL_VERSION;
  strncpy(login_req.devname, devname, 32);
  login_req.devname[31] = 0;
  CPU_TO_BE_LOGIN_REQ(&login_req);
  if (retry_write(sock, &login_req, sizeof(login_req)) < 0){
    tell_err("transfer of login request to %s failed : %s\n", host,
             strerror(errno));
    return -1;
  }
  if (retry_write(sock, &node, sizeof(node)) < 0){
    tell_err("transfer of my node name to %s failed : %s\n", host,
             strerror(errno));
    return -1;
  }
  if (retry_read(sock, &login_reply, sizeof(login_reply)) < 0){
    tell_err("transfer of login reply from %s failed : %s\n", host,
             strerror(errno));
    return -1;
  }
  BE_LOGIN_REPLY_TO_CPU(&login_reply);
  if (login_reply.err){
    if (login_reply.version)
      fail("Protocol mismatch: client using version %u, " 
           "server using version %u", PROTOCOL_VERSION, login_reply.version);
    else if (login_reply.err == ENODEV){
      tell_err("login refused by the server : %s\n",
               strerror(login_reply.err));
      return -1;
    }
    else
      fail("login refused by the server, quitting : %s\n",
           strerror(login_reply.err));
  }
  snprintf(sysfs_buf, 4096, "%" PRIu64, login_reply.sectors);
  if (do_set_sysfs_attr(minor_num, "sectors", sysfs_buf) < 0)
    fail("cannot set /sys/class/gnbd/gnbd%d/sectors value : %s\n", minor_num,
         strerror(errno));
  return 0;
}

void do_receiver(void)
{
  char host[256];
  unsigned short port;
  int sock;
  do_it_req_t req;

  /* FIXME -- I don't think that these can ever return a failure when
     a simple retry would work... if they can, then I should not exit */
  if (do_get_sysfs_attr(minor_num, "server") == NULL)
    fail("cannot get /sys/class/gnbd/gnbd%d/server value : %s\n", minor_num,
         strerror(errno));
  if (parse_server(sysfs_buf, host, (uint16_t *)&port) < 0)
    fail("cannot parse /sys/class/gnbd/gnbd%d/server\n", minor_num);
  if (kill_old_gservs(host, port) < 0)
    return;
  if( (sock = connect_to_server(host, port)) < 0){
    tell_err("cannot connect to %s (%d) : %s\n", host, sock,
             strerror(errno));
    return;
  }
  if (gnbd_login(sock, host) < 0){
    close(sock);
    return;
  }
  if (!have_connected){
    if (daemon_mode)
      finish_startup("gnbd_recvd started\n");
    else
      printm("gnbd_recvd started\n");
    have_connected = 1;
  } 
  req.minor = minor_num;
  req.sock_fd = sock;
  if (ioctl(devfd, GNBD_DO_IT, &req) == 0){
    log_msg("client shutting down connect with %s\n", host);
    exit(0);
  }
  log_msg("client lost connection with %s : %s\n", host, strerror(errno));
  close(sock);
  return;
}

int main(int argc, char **argv)
{
  parse_cmdline(argc, argv);
  char minor_str[20];
  struct sigaction act;
  
  if (daemon_mode)
    daemonize_and_exit_parent();

  memset(&act,0,sizeof(act));
  act.sa_handler = sig_hup;
  if (sigaction(SIGHUP, &act, NULL) < 0)
    fail("cannot set the signal handler for SIGHUP : %s\n", strerror(errno));

  /* FIXME -- is this necessary */
  unblock_sighup();

  if (get_my_nodename(node_name, is_clustered) < 0)
    fail("cannot get node name : %s\n", strerror(errno));

  snprintf(minor_str, 20, "-%u", minor_num);
  minor_str[19] = 0;

  if (!pid_lock(minor_str))
    fail("%s already running for device #%d\n", program_name,
         minor_num);

  if (forget_mode){
    finish_startup("gnbd_recvd started\n");
    have_connected = 1;
  }

  get_devname();
  open_device();

  /* FIXME -- setup signals here */
  /* FIXME -- somewhere I should set myself to nobody */
  
  while(1){
    do_receiver();
    if (!have_connected)
      fail("quitting\n");
    tell_msg("reconnecting\n");
    if (!got_sighup)
      sleep(5);
    got_sighup = 0;
  }

  exit(0);
}
