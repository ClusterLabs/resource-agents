#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <syslog.h>
#include <sys/utsname.h>
#include <string.h>
#include <errno.h>
#include <inttypes.h>

#include "libcman.h"
#include "gnbd_endian.h"
#include "gnbd_utils.h"


pid_t program_pid;
char *program_name;
verbosity_level verbosity = NORMAL;
char *program_dir = "/var/run/gnbd";
int daemon_status;
char ip_str[16];
char sysfs_buf[4096];


char *beip_to_str(ip_t ip)
{
  int i;
  char *p;

  p = ip_str;
  ip = beip_to_cpu(ip);

  for (i = 3; i >= 0; i--)
  {
    p += sprintf(p, "%d", (ip >> (8 * i)) & 0xFF);
    if (i > 0)
      *(p++) = '.';
  }
  return ip_str;
}

static void sig_usr1(int sig)
{
  daemon_status = 0;
}

static void sig_usr2(int sig)
{
  daemon_status = 1;
}

/*buffer must be 65 charaters long */
int get_my_nodename(char *buf, int is_clustered){
  struct utsname nodeinfo;

  if (is_clustered) {
    int r = -1;
    cman_handle_t ch;
    cman_node_t node;
    ch = cman_init(NULL);
    if (!ch){
      log_err("cman_init failed : %s\n", strerror(errno));
      return -1;
    }
    memset(&node, 0, sizeof(node));
    if (cman_get_node(ch, CMAN_NODEID_US, &node) < 0){
      log_err("cman_get_node failed : %s\n", strerror(errno));
      goto out;
    }
    strncpy(buf, node.cn_name, 64);
    buf[64] = 0;
    r = 0;
   out:
    cman_finish(ch);
    return r;
  }
  if (uname(&nodeinfo) < 0)
    /*FIXME -- can I print something out here?? */
    return -1;
  strcpy(buf, nodeinfo.nodename);
  return 0;
}



int __check_lock(char *file, int *pid){
  int fd;
  char path[1024];
  struct flock lock;

  snprintf(path, 1024, "%s/%s", program_dir, file);
  
  if (pid)
    *pid = 0;

  if( (fd = open(path, O_RDWR)) < 0){
    if (errno != ENOENT){
      return -1;
    }
    return 0;
  }
  lock.l_type = F_WRLCK;
  lock.l_start = 0;
  lock.l_whence = SEEK_SET;
  lock.l_len = 0;

  if (fcntl(fd, F_GETLK, &lock) < 0)
    goto fail;

  if (pid && lock.l_type != F_UNLCK){
    char pid_str[13];
    int count = 0;
    int bytes;
    
    memset(pid_str, 0, 13);
    while( (bytes = read(fd, &pid_str[count], 12 - count)) != 0){
      if (bytes <= 0 && errno != -EINTR)
        goto fail;
      if (bytes > 0)
        count += bytes;
    }
    if (sscanf(pid_str, "%d\n", pid) != 1){
      errno = -EINVAL;
      goto fail;
    }
  }
  
  close(fd);

  if (lock.l_type == F_UNLCK)
    return 0;
  return 1;

 fail:
  close(fd);
  return -1;
}

int check_lock(char *file, int *pid){
  int ret;

  ret = __check_lock(file, pid);
  if (ret < 0){
    printe("cannot check lock for %s/%s : %s\n", program_dir, file,
           strerror(errno));
    exit(1);
  }
  return ret;
}

/**
 * pid_lock - there can be only one.
 * returns 1 - you locked the file
 *         0 - another process is already running
 */
int pid_lock(char *extra_info)
{
   struct flock lock;
   char pid_str[12], path[1024];
   int fd, val;
   
   if (strncmp(program_dir, "/var/run/gnbd", 13) == 0){
     struct stat stat_buf;
     
     if (stat("/var/run/gnbd", &stat_buf) < 0){
       if (errno != ENOENT)
         fail_startup("cannot stat lockfile dir : %s\n", strerror(errno));
       if(mkdir("/var/run/gnbd", S_IRWXU))
         fail_startup("cannot create lockfile directory : %s\n",
                      strerror(errno));
     }
     else if(!S_ISDIR(stat_buf.st_mode))
       fail_startup("/var/run/gnbd is not a directory.\n"
                   "Cannot create lockfile.\n");
   }

   snprintf(path, 1024, "%s/%s%s.pid", program_dir, program_name, extra_info);

   if( (fd = open(path, O_WRONLY | O_CREAT,
                  (S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH))) < 0)
     fail_startup("cannot open lockfile '%s' : %s\n", path, strerror(errno));

   lock.l_type = F_WRLCK;
   lock.l_start = 0;
   lock.l_whence = SEEK_SET;
   lock.l_len = 0;

   if (fcntl(fd, F_SETLK, &lock) < 0) {
     if (errno == EACCES || errno == EAGAIN){
       close(fd);
       return 0;
     }
     else
       fail_startup("cannot lock lockfile : %s\n", strerror(errno));
   }
   
   if (ftruncate(fd, 0) < 0)
      fail_startup("cannot truncate lockfile : %s\n", strerror(errno));

   snprintf(pid_str, 12, "%d\n", getpid());
   if (write(fd, pid_str, strlen(pid_str)) != strlen(pid_str))
      fail_startup("error writing to '%s' : %s\n", path, strerror(errno));

   if ((val = fcntl(fd, F_GETFD, 0)) < 0)
      fail_startup("cannot read close-on-exec flag : %s\n", strerror(errno));

   val |= FD_CLOEXEC;
   if (fcntl(fd, F_SETFD, val) < 0)
      fail_startup("cannot set close-on-exec flag : %s\n", strerror(errno));
   return 1;
}

/* This was gleaned from lock_gulmd */
int daemonize(void)
{
  int pid, i;
 
  if( (pid = fork()) < 0){
    printe("Failed first fork: %s\n", strerror(errno));
    return -1;
  }
  else if (pid != 0){
    return pid;
  }

  setsid();

  if ( (pid = fork()) < 0){
    printe("Failed second fork: %s\n", strerror(errno));
    exit(1);
  }
  else if (pid != 0)
    exit(0);

  chdir("/");
  umask(0);
  /* leave default fds open until startup is completed */
  for(i = open_max()-1; i > 2; --i)
    close(i);  
  openlog(program_name, LOG_PID, LOG_DAEMON);
  return 0;
}

void daemonize_and_exit_parent(void)
{
  int i_am_parent;
  struct sigaction act;

  program_pid = getpid();
  memset(&act,0,sizeof(act));
  act.sa_handler = sig_usr1;
  if (sigaction(SIGUSR1, &act, NULL) < 0){
    printe("cannot set a handler for SIGUSR1 : %s\n", strerror(errno));
    exit(1);
  }
  memset(&act,0,sizeof(act));
  act.sa_handler = sig_usr2;
  if (sigaction(SIGUSR2, &act, NULL) < 0){
    printe("cannot set a handler for SIGUSR2 : %s\n", strerror(errno));
    exit(1);
  }
  daemon_status = -1;
  i_am_parent = daemonize();
  if (i_am_parent < 0)
    exit(1);
  if (i_am_parent){
    while(daemon_status == -1)
      sleep(10);
    exit(daemon_status);
  }
  memset(&act,0,sizeof(act));
  act.sa_handler = SIG_DFL;
  if (sigaction(SIGUSR1, &act, NULL) < 0)
    fail_startup("cannot set default handler for SIGUSR1 : %s\n",
                 strerror(errno));
  memset(&act,0,sizeof(act));
  act.sa_handler = SIG_DFL;
  if (sigaction(SIGUSR2, &act, NULL) < 0)
    fail_startup("cannot set default handler for SIGUSR2 : %s\n",
                 strerror(errno));
}

int parse_server(char *buf, char *name, uint16_t *port)
{
  char *ptr;

  if (strlen(buf) == 0){
    strcpy(name, "");
    *port = 0;
    return 0;
  }

  ptr = strchr(buf, '/');
  if (!ptr)
    return -1;
  *ptr++ = 0;
  strncpy(name, buf, 256);
  if (sscanf(ptr, "%4hx", port) != 1)
    return -1;
  return 0;
}

char *do_get_sysfs_attr(int minor, char *attr_name)
{
  int sysfs_fd;
  int bytes;
  int count = 0;
  char sysfs_path[40];
  
  snprintf(sysfs_path, 40, "/sys/class/gnbd/gnbd%d/%s", minor, attr_name);
  if( (sysfs_fd = open(sysfs_path, O_RDONLY)) < 0)
    return NULL;
  while (count < 4095){
    bytes = read(sysfs_fd, &sysfs_buf[count], 4095 - count);
    if (bytes < 0 && errno != EINTR){
      close (sysfs_fd);
      return NULL;
    }
    if (bytes == 0)
      break;
    count += bytes;
  }
  /* overwrite the '\n' with '\0' */
  sysfs_buf[count - 1] = 0;
  if (close(sysfs_fd) < 0)
    return NULL;
  return sysfs_buf;
}

char *get_sysfs_attr(int minor, char *attr_name)
{
  char *buf;
  buf = do_get_sysfs_attr(minor, attr_name);
  if (buf == NULL){
    printe("cannot get /sys/class/gnbd/gnbd%d/%s value : %s\n",
           minor, attr_name, strerror(errno));
    exit(1);
  }
  return buf;
}

int do_set_sysfs_attr(int minor_nr, char *attribute, char *val)
{
  int sysfs_fd;
  int bytes;
  int count = 0;
  char sysfs_path[40];
  int len;

  len = strlen(val);
  if (len >= 4096)
    return -1;
  snprintf(sysfs_path, 40, "/sys/class/gnbd/gnbd%d/%s", minor_nr, attribute);
  if( (sysfs_fd = open(sysfs_path, O_WRONLY)) < 0)
    return -1;
  while (count < len){
    bytes = write(sysfs_fd, &val[count], len - count);
    if (bytes < 0 && errno != EINTR){
      close(sysfs_fd);
      return -2;
    }
    if (bytes == 0){
      close(sysfs_fd);
      return -1;
    }
    count += bytes;
  }
  if (close(sysfs_fd) < 0)
    return -1;
  return 0;
}

/* This version allows writes to fail */
int __set_sysfs_attr(int minor_nr, char *attribute, char *val)
{
  int err = do_set_sysfs_attr(minor_nr, attribute, val);
  if (err == -1){
    printe("cannot set /sys/class/gnbd/gnbd%d/%s value : %s\n", minor_nr,
           attribute, strerror(errno));
    exit(1);
  }
  return err; 
}

void set_sysfs_attr(int minor_nr, char *attribute, char *val)
{
  if (do_set_sysfs_attr(minor_nr, attribute, val) < 0){
    printe("cannot set /sys/class/gnbd/gnbd%d/%s value : %s\n", minor_nr,
           attribute, strerror(errno));
    exit(1);
  }
}

#ifdef OPEN_MAX
static int openmax = OPEN_MAX;
#else
static int openmax = 0;
#endif /* OPEN_MAX */

#define OM_GUESS 256
/**
 * open_max - clacs max number of open files.
 * Returns: the maximum number of file we can have open at a time.
 *          Or -1 for error.
 */
int open_max(void)
{
   if(openmax == 0) {
      errno = 0;
      if((openmax = sysconf(_SC_OPEN_MAX)) < 0) {
         if( errno == 0) {
            openmax = OM_GUESS;
         }else{
            return -1;
         }
      }
   }
   return openmax;
}
