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
#include <linux/gnbd.h>

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
char sysfs_base[25];
char sysfs_path[40];
char devname[32];

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

  while((c = getopt(argc, argv, "dfhqvV")) != -1){
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
      printf("%s %s (built %s %s)\n", program_name, GNBD_RELEASE_NAME,
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
  FILE *server_file;

  snprintf(sysfs_path, 40, "%s/name", sysfs_base);
  if( (server_file = fopen(sysfs_path, "r")) == NULL)
    fail("cannot open %s : %s\n", sysfs_path, strerror(errno));
  if (fscanf(server_file, "%31s\n", devname) != 1)
    fail("cannot read device name from %s\n", sysfs_path);
  fclose(server_file);
}

void open_device(void)
{
  if( (devfd = open("/dev/gnbd_ctl", O_RDONLY)) < 0)
    fail("cannot open gnbd control device : %s\n", strerror(errno));
  if (ioctl(devfd, GNBD_GET_TIME, &timestamp) < 0)
    fail("cannot get timestamp : %s\n", strerror(errno));
}

int connect_to_server(ip_t server_ip, unsigned short port)
{
  int sock;
  struct sockaddr_in addr;
  int trueint = 1;
  
  if( (sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0){
    tell_err("cannot create socket : %s\n", strerror(errno));
    return -1;
  }
  
  if (setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, &trueint, sizeof(int)) < 0){
    tell_err("cannot set socket options : %s\n", strerror(errno));
    close(sock);
    return -1;
  }
  
  addr.sin_family = AF_INET;
  addr.sin_port = htons(port);
  addr.sin_addr.s_addr = server_ip;
  
  if(connect(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0){
    tell_err("cannot connect to server %s : %s\n",
             beip_to_str(server_ip), strerror(errno));
    close(sock);
    return -1;
  }
  return sock;
}

int kill_old_gservs(ip_t server_ip, unsigned short port)
{
  int sock;
  device_req_t device;
  uint32_t cmd = EXTERN_KILL_GSERV_REQ;

  if( (sock = connect_to_server(server_ip, port)) < 0)
    return -1;
  if (send_u32(sock, cmd) < 0){
    tell_err("cannot send loign request to %s : %s\n",
             beip_to_str(server_ip), strerror(errno));
    goto fail;
  }
  if (retry_write(sock, &device, sizeof(device)) < 0){
    tell_err("transfer of device name to %s failed : %s\n",
             beip_to_str(server_ip), strerror(errno));
    goto fail;
  }
  if (recv_u32(sock, &cmd) < 0){
    tell_err("reading kill server reply from %s failed : %s\n",
             beip_to_str(server_ip), strerror(errno));
    goto fail;
  }
  if (cmd && cmd != ENODEV){
    tell_err("kill server request from %s failed : %s\n",
             beip_to_str(server_ip), strerror(errno));
    goto fail;
  }
  close(sock);
  return 0;

 fail:
  close(sock);
  return -1;
}


/* If communication fails, retry.  If I receive a failure reply, just exit */
int gnbd_login(int sock, ip_t server_ip)
{
  login_req_t login_req;
  login_reply_t login_reply;
  uint32_t cmd = EXTERN_LOGIN_REQ;
  FILE *server_file;

  if (send_u32(sock, cmd) < 0){
    tell_err("cannot send loign request to %s : %s\n",
             beip_to_str(server_ip), strerror(errno));
    return -1;
  }

  login_req.timestamp = timestamp;
  login_req.version = PROTOCOL_VERSION;
  strncpy(login_req.devname, devname, 32);
  login_req.devname[31] = 0;
  CPU_TO_BE_LOGIN_REQ(&login_req);
  if (retry_write(sock, &login_req, sizeof(login_req)) < 0){
    tell_err("transfer of login request to %s failed : %s\n",
             beip_to_str(server_ip), strerror(errno));
    return -1;
  }
  if (retry_read(sock, &login_reply, sizeof(login_reply)) < 0){
    tell_err("transfer of login reply from %s failed : %s\n",
             beip_to_str(server_ip), strerror(errno));
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
  snprintf(sysfs_path, 40, "%s/sectors", sysfs_base);
  if( (server_file = fopen(sysfs_path, "w")) == NULL)
    fail("cannot open %s : %s\n", sysfs_path, strerror(errno));
  if (fprintf(server_file, "%llu\n", login_reply.sectors) < 0)
    fail("cannot write sector value %llu to %s : %s\n", login_reply.sectors,
         sysfs_path, strerror(errno));
  fclose(server_file);
  return 0;
}

void do_receiver(void)
{
  ip_t server_ip;
  unsigned short port;
  FILE *server_file;
  int sock;
  do_it_req_t req;
  int res;

  /* FIXME -- I don't think that these can ever return a failure when
     a simple retry would work... if they can, then I should not exit */
  snprintf(sysfs_path, 40, "%s/server", sysfs_base);
  if( (server_file = fopen(sysfs_path, "r")) == NULL)
    fail("cannot open %s : %s\n", sysfs_path, strerror(errno));
  if( (res = fscanf(server_file, "%8lx:%hx\n", (unsigned long *)&server_ip,
             &port)) != 2)
    fail("cannot read server information from %s (%d)\n", sysfs_path, res);
  fclose(server_file);
  server_ip = cpu_to_beip(server_ip);
  /* FIXME -- I should pull the above stuff into connect_to_server.
     It would keep this function easy to read */
  if (kill_old_gservs(server_ip, port) < 0)
    return;
  if( (sock = connect_to_server(server_ip, port)) < 0)
    return;
  if (gnbd_login(sock, server_ip) < 0){
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
    log_msg("client shutting down connect with %s\n", beip_to_str(server_ip));
    exit(0);
  }
  log_msg("client lost connection with %s : %s\n", beip_to_str(server_ip),
          strerror(errno));
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

  snprintf(minor_str, 20, "-%u", minor_num);
  minor_str[19] = 0;

  if (!pid_lock(minor_str))
    fail("%s already running for device #%d\n", program_name,
         minor_num);
  get_devname();
  open_device();

  /* FIXME -- setup signals here */
  /* FIXME -- somewhere I should set myself to nobody */
  
  if (forget_mode){
    finish_startup("gnbd_recvd started\n");
    have_connected = 1;
  }
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
