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
#include <limits.h>
#include <dirent.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <sys/utsname.h>
#include <inttypes.h>

#include "gnbd.h"
#include "fence_return.h"
#include "gnbd_endian.h"
#include "gnbd_utils.h"
#include "list.h"
#include "extern_req.h"
#include "gnbd_monitor.h"
#include "trans.h"

#include "copyright.cf"

#define MAN_MSG   "Please see man page for details.\n"

struct gnbd_info_s{
  char name[32];
  int minor_nr;
  char server_name[256];
  uint16_t port;
  int usage;
  int connected;
  long waittime;
  unsigned short flags;
  int pid;
  uint64_t sectors;
  list_t list;
};
typedef struct gnbd_info_s gnbd_info_t;

#define IS_UNUSED(x) ((x)->pid < 0 && (x)->sectors == 0 && (x)->usage == 0)
#define IS_READONLY(x) ((x)->flags && GNBD_READ_ONLY)

int gnbd_major = -1;
list_t gnbd_list;
/* FIXME -- why unsigned int */
unsigned int server_port = 14567;
int maxminor = -1;
char *sysfs_class = "/sys/class/gnbd";
int override = 0;
char node_name[65];
int is_clustered = 1;

#define MODE_MASK (S_IRWXU | S_IRWXG | S_IRWXO)

#define ACTION_FENCE         1
#define ACTION_REMOVE        2
#define ACTION_IMPORT        3
#define ACTION_LIST          4
#define ACTION_VALIDATE      5
#define ACTION_LIST_EXPORTED 6
#define ACTION_REMOVE_ALL    7
#define ACTION_UNFENCE       8
#define ACTION_LIST_BANNED   9
#define ACTION_FAIL_SERVER  10
#define ACTION_GET_UID      11

gnbd_info_t *match_info_name(char *name){
  list_t *item;
  gnbd_info_t *gnbd_entry;

  list_foreach(item, &gnbd_list){
    gnbd_entry = list_entry(item, gnbd_info_t, list);
    if (strncmp(gnbd_entry->name, name, 32) == 0)
      return gnbd_entry;
  }
  return NULL;
}



int talk_to_server(char *host, uint32_t request, char **buf, void *request_data,
                     ssize_t request_data_size)
{
  int sock_fd;
  int n, total = 0;
  uint32_t msg;

  *buf = NULL;
  sock_fd = connect_to_server(host, (uint16_t)server_port);
  if (sock_fd < 0){
    printe("cannot connect to server %s (%d) : %s\n", host, sock_fd,
           strerror(errno));
    exit(1);
  }
  msg = cpu_to_be32(request);
  if (write(sock_fd, &msg, sizeof(msg)) != sizeof(msg)){
    printe("error sending request to %s : %s\n", host, strerror(errno));
    exit(1);
  }
  if (request_data){
    if (write(sock_fd, request_data, request_data_size) != request_data_size){
      printe("error sending request data to %s : %s\n", host, strerror(errno));
      exit(1);
    }
  }
  if (read(sock_fd, &msg, sizeof(msg)) != sizeof(msg)){
    printe("error reading reply from %s : %s\n", host, strerror(errno));
    exit(1);
  }
  msg = cpu_to_be32(msg);
  if (msg != EXTERN_SUCCESS_REPLY){
    printe("request to %s failed : %s\n", host, strerror(REPLY_ERR(msg)));
    exit(1);
  }
  if (read(sock_fd, &msg, sizeof(msg)) != sizeof(msg)){
    printe("error reading size of reply from %s : %s\n", host,
           strerror(errno));
    exit(1);
  }
  msg = be32_to_cpu(msg);
  if (!msg)
    goto exit;
  *buf = malloc(msg);
  if (*buf == NULL){
    printe("couldn't allocate memory for server reply : %s\n", strerror(errno));
    exit(1);
  }
  memset(*buf, 0, msg);
  while(total < msg){
    n = read(sock_fd, *buf + total, msg - total);
    if (n <= 0){
      printe("error reading reply data from server %s : %s\n", host,
             strerror(errno));
      exit(1);
    }
    total += n;
  }
exit:
  close(sock_fd);
  return total;
}

#define read_from_server(host, request, buf) \
  talk_to_server(host, request, buf, NULL, 0)

int start_gnbd_monitor(int minor_nr, int timeout, char *host)
{
  int ret;
  char *serv_node;
  char cmd[256];

  /* no timeout, no monitor */
  if (!timeout) 
    return 0;

  read_from_server(host, EXTERN_NODENAME_REQ, &serv_node);
  if (!serv_node) {
    printe("got empty server name\n");
    return -1;
  }

  snprintf(cmd, 256, "gnbd_monitor %d %d %s", minor_nr, timeout, serv_node);
  ret = system(cmd);
  free(serv_node);
  if( ret < 0){
    printe("system() failed : %s\n", strerror(errno));
    return -1;
  }
  if (ret != 0){
    printe("gnbd_monitor failed\n");
    return -1;
  }
  return 0;
}

int match_info_minor(int minor_nr){
  list_t *item;
  gnbd_info_t *gnbd_entry;

  list_foreach(item, &gnbd_list){
    gnbd_entry = list_entry(item, gnbd_info_t, list);
    if (gnbd_entry->minor_nr == minor_nr)
      return 1;
  }
  return 0;
}

void info_add(gnbd_info_t *info)
{
  list_t *item;
  gnbd_info_t *gnbd_entry;

  list_foreach(item, &gnbd_list){
    gnbd_entry = list_entry(item, gnbd_info_t, list);
    if (gnbd_entry->minor_nr > info->minor_nr)
      break;
  }
  list_add_prev(&info->list, item);
}

char action_to_flag(int action){
  switch(action){
  case ACTION_FENCE:
    return 's';
  case ACTION_REMOVE:
    return 'r';
  case ACTION_IMPORT:
    return 'i';
  case ACTION_LIST:
    return 'l';
  case ACTION_VALIDATE:
    return 'a';
  case ACTION_LIST_EXPORTED:
    return 'e';
  case ACTION_REMOVE_ALL:
    return 'R';
  case ACTION_UNFENCE:
    return 'u';
  case ACTION_LIST_BANNED:
    return 'c';
  case ACTION_FAIL_SERVER:
    return 'k';
  default:
    printe("invalid action value\n");
    return 0;
  }
}

#define set_action(x) \
do{ \
  if (action){ \
    printe("flags -%c and -%c are not compatible\n", action_to_flag(action), \
           action_to_flag(x)); \
    fprintf(stderr, "Please see man page for details.\n"); \
    return 1; \
  } \
  action = (x); \
} while(0)


int usage(void){
  printf(
"Usage:\n"
"\n"
"gnbd_import [options]\n"
"\n"
"Options:\n"
"  -a               validate all imported GNBDs, and remove the invalid ones\n"
"  -c <server>      list all nodes currently IO fenced from the server\n"
"  -e <server>      list all GNBDs exported by the server\n"
"  -h               print this help message\n"
"  -i <server>      import all GNBDs from the server\n"
"  -l               list all imported GNBDs [default action]\n"
"  -n               No cluster. Do not contact the cluster manager\n"
"  -O               Override prompts\n"
"  -p <port>        change the port to check on the GNBD server [default 14567]\n"
"  -q               quiet mode.\n"
"  -R               remove all imported GNBDs\n"
"  -r [GNBD | list] remove specified GNBD(s)\n"
"  -s <host>        IO fence the host (from known servers by default)\n"
"  -t <server>      specify a server for the IO fence (with -s or -u)\n"
"  -u <host>        unfence the host (from known servers by default)\n"
"  -U <GNBD>        get the Universal Identifier for the specified GNBD\n"
"  -V               version information\n"
"  -v               verbose output\n"
"\n");
  return 0;
}

int get_major_nr(void){
  char device[LINE_MAX];
  char buf[LINE_MAX];
  int major_nr;
  FILE *fp;
  fp = fopen("/proc/devices", "r");
  if (!fp){
    printe("could not open /proc/devices: %s\n", strerror(errno));
    exit(1);
  }
  while(fgets(buf, LINE_MAX, fp) != NULL)
    if (sscanf(buf, "%d %s", &major_nr, device) == 2 &&
	strcmp(device, "gnbd") == 0)
      break;
  if(strcmp(device, "gnbd") == 0)
    return major_nr;
  printe("could not find gnbd registered in /proc/devices.  This\n"
         "probably means that you have not loaded the gnbd module.\n");
  exit(1);
}

/* On success 0 is returned, on failure, -1 is returned and the
   device is removed if it existed */
int get_dev(char *path, mode_t *mode, int *major_nr, int *minor_nr){
  struct stat stats;

  if (stat(path, &stats) < 0){
    if (errno != ENOENT){
      printe("cannot stat %s : %s\n", path, strerror(errno));
      exit(1);
    }
    return -1;
  }
  if ((stats.st_mode & S_IFMT) != (*mode & S_IFMT)){
    printv("incorrect type for %s. removing\n", path);
    goto out_remove;
  }
  if ((*mode & MODE_MASK) && ((stats.st_mode & MODE_MASK) !=
                              (*mode & MODE_MASK))){
    printv("incorrect mode for %s. removing\n", path);
    goto out_remove;
  }
  if ((*major_nr >= 0 && major(stats.st_rdev) != *major_nr) ||
      (*minor_nr >= 0 && minor(stats.st_rdev) != *minor_nr)){
    printv("incorrect major/minor number for %s. removing\n", path);
    goto out_remove;
  }
  *major_nr = major(stats.st_rdev);
  *minor_nr = minor(stats.st_rdev);
  *mode = stats.st_mode;
  return 0;

 out_remove:
  if (unlink(path) < 0){
    printe("cannot remove %s : %s\n", path, strerror(errno));
    exit(1);
  }
  return -1;
}

void check_gnbd_ctl(void)
{
  char device[LINE_MAX];
  char buf[LINE_MAX];
  int major_nr = 10;
  int minor_nr;
  mode_t mode = S_IFCHR | S_IRUSR | S_IWUSR;
  FILE *fp;

  fp = fopen("/proc/misc", "r");
  if (!fp){
    printe("could not open /proc/misc : %s\n", strerror(errno));
    exit(1);
  }
  while(fgets(buf, LINE_MAX, fp) != NULL)
    if (sscanf(buf, "%d %s", &minor_nr, device) == 2 &&
	strcmp(device, "gnbd_ctl") == 0)
      break;
  if(strcmp(device, "gnbd_ctl") != 0){
    printe("could not find gnbd_ctl registered in /proc/misc.  This\n"
           "probably means that you have not loaded the gnbd module.\n");
    exit(1);
  }
  if (get_dev("/dev/gnbd_ctl", &mode, &major_nr, &minor_nr) == 0)
    return;
  printv("creating /dev/gnbd_ctl\n");
  if (mknod("/dev/gnbd_ctl", mode, makedev(10, minor_nr)) < 0){
    printe("cannot create gnbd_ctl : %s\n", strerror(errno));
    exit(1);
  }
}
  
void create_gnbd_dir(void){
  if(mkdir("/dev/gnbd", S_IRWXU | S_IRGRP | S_IXGRP |
           S_IROTH | S_IXOTH) == -1){
    if (errno == EEXIST)
      return;
    else{
      printe("unable to create directory /dev/gnbd : %s\n", strerror(errno));
      exit(1);
    }
  }
  printm("created directory /dev/gnbd\n");
}

int find_empty_minor(){
  int minor;
  gnbd_info_t info;

  for (minor = 0; minor < MAX_GNBD; minor++){
    if (sscanf(get_sysfs_attr(minor, "usage"), "%d", &info.usage) != 1){
      printe("cannot parse %s/gnbd%d/usage\n", sysfs_class, minor);
      exit(1);
    }
    if (info.usage)
      continue;
    if (sscanf(get_sysfs_attr(minor, "pid"), "%d", &info.pid) != 1){
      printe("cannot parse %s/gnbd%d/pid\n", sysfs_class, minor);
      exit(1);
    }
    if (info.pid != -1)
      continue; 
    if (sscanf(get_sysfs_attr(minor, "sectors"), "%"PRIu64,
               &info.sectors) != 1){
      printe("cannot parse %s/gnbd%d/sectors\n", sysfs_class, minor);
      exit(1);
    }
    if (info.sectors == 0)
      return minor;
  }
  printe("No available minor numbers\n");
  exit(1);
}
  

int get_info(gnbd_info_t *entry)
{
  int minor = entry->minor_nr;
  if (entry->name[0] != 0){
    if (strncmp(entry->name, get_sysfs_attr(minor, "name"), 32) != 0){
      printv("/dev/gnbd/%s doesn't match up with %s/gnbd%d entry\n",
             entry->name, sysfs_class, minor);
      return -1;
    }
  } else{
    strncpy(entry->name, get_sysfs_attr(minor, "name"), 32);
    entry->name[31] = 0;
  }
  if (parse_server(get_sysfs_attr(minor, "server"), entry->server_name,
                   &entry->port) < 0){
    printe("cannot parse %s/gnbd%d/server\n", sysfs_class, minor);
    exit(1);
  }
  if (sscanf(get_sysfs_attr(minor, "sectors"), "%"PRIu64,
             &entry->sectors) != 1){
    printe("cannot parse %s/gnbd%d/sectors\n", sysfs_class, minor);
    exit(1);
  }
  if (sscanf(get_sysfs_attr(minor, "usage"), "%d\n", &entry->usage) != 1){
    printe("cannot parse %s/gnbd%d/usage\n", sysfs_class, minor);
    exit(1);
  }
  if (sscanf(get_sysfs_attr(minor, "connected"), "%d", &entry->connected) != 1){
    printe("cannot parse %s/gnbd%d/connected\n", sysfs_class, minor);
    exit(1);
  }
  if (sscanf(get_sysfs_attr(minor, "waittime"), "%ld", &entry->waittime) != 1){
    printe("cannot parse %s/gnbd%d/waittime\n", sysfs_class, minor);
    exit(1);
  }
  if (sscanf(get_sysfs_attr(minor, "flags"), "%hx", &entry->flags) != 1){
    printe("cannot parse %s/gnbd%d/flags\n", sysfs_class, minor);
    exit(1);
  }
  if (sscanf(get_sysfs_attr(minor, "pid"), "%d", &entry->pid) != 1){
    printe("cannot parse %s/gnbd%d/pid\n", sysfs_class, minor);
    exit(1);
  }
  return 0;
}

int add_device(char *name)
{
  gnbd_info_t *entry;
  int minor_nr = -1;
  char path[LINE_MAX];
  mode_t mode = S_IFBLK;

  snprintf(path, LINE_MAX, "/dev/gnbd/%s", name);

  if (get_dev(path, &mode, &gnbd_major, &minor_nr) < 0)
    return -1;
  
  entry = (gnbd_info_t *)malloc(sizeof(gnbd_info_t));
  if (!entry){
    printe("couldn't allocated space for %s device information : %s\n",
           name, strerror(errno));
    exit(1);
  }
  strncpy(entry->name, name, 32);
  entry->name[31] = 0;
  entry->minor_nr = minor_nr;
  
  if (get_info(entry) < 0){
    printv("deleting %s\n", path);
    goto exit_unlink;
  }

  if (IS_UNUSED(entry)){
    printv("%s is not in use. deleting\n", path);
    goto exit_unlink;
  }
 
  if (entry->minor_nr > maxminor)
    maxminor = entry->minor_nr;
  info_add(entry);
  return 0;

 exit_unlink: 
  if(unlink(path) < 0){
    printe("cannot delete %s : %s\n", path, strerror(errno));
    exit(1);
  }
  free(entry);
  
  return -1;
}

void create_generic_gnbd_file(int minor)
{
  int err;
  char path[LINE_MAX];
  sprintf(path, "/dev/gnbd%d", minor);
 retry:
  err = mknod(path, S_IFBLK | S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH,
              makedev(gnbd_major, minor));
  if (err < 0 && errno != EEXIST){
    printe("could not create file %s : %s\n",
           path, strerror(errno));
    exit(1);
  }
  if (err < 0){
    struct stat stats;
    if (stat(path, &stats) < 0){
      printe("couldn't stat file %s : %s\n", path, strerror(errno));
      exit(1);
    }
    if (major(stats.st_rdev) != gnbd_major || minor(stats.st_rdev) != minor){
      if (unlink(path) < 0){
        printe("couldn't unlink file %s : %s\n", path, strerror(errno));
        exit(1);
      }
      goto retry;
    }
  }
}

void cleanup_device(char *name, int minor, int fd)
{
  char path[LINE_MAX];

  if (ioctl(fd, GNBD_CLEAR_QUE, (unsigned long)minor) < 0){
    printe("cannot clear gnbd device #%d queue : %s\n", minor,
           strerror(errno));
    exit(1);
  }
  if (override){
    printm("waiting for all users to close device %s\n", name);
    while (1){
      int usage;
      if (sscanf(get_sysfs_attr(minor, "usage"), "%d\n", &usage) != 1){
        printe("cannot parse %s/gnbd%d/usage\n", sysfs_class, minor);
        exit(1);
      }
      if (usage == 0)
        break;
      sleep(2);
    }
  }
  set_sysfs_attr(minor, "sectors", "0\n");
  if (name){
    snprintf(path, LINE_MAX, "/dev/gnbd/%s", name);
    if (unlink(path) < 0 && errno != ENOENT){
      printe("cannot remove fale %s : %s\n", path, strerror(errno));
      exit(1);
    }
    printm("removed gnbd device %s\n", name);
  }
}


#define MULTIPATH_CTL "/sbin/mpath_ctl"
void stop_mpath_monitoring(int minor_nr)
{
  int i_am_parent, fd;
  char args[32];

  i_am_parent = daemonize();
  if (i_am_parent < 0){
    printm("cannot daemonize to exec multipath code : %s\n", strerror(errno));	
    return;
  }
  if (i_am_parent)
    return;
  if ((fd = open("/dev/null", O_RDWR)) < 0) {
    printm("cannot open /dev/null in daemon : %s\n", strerror(errno));
    exit(1);
  }
  dup2(fd, 0);
  dup2(fd, 1);
  dup2(fd, 2);
  if (fd > 2)
    close(fd);
  snprintf(args, 31, "remove path gnbd%d", minor_nr);
  args[31] = '\0';
  execl(MULTIPATH_CTL, MULTIPATH_CTL, args, NULL);
  log_verbose("cannot exec %s : %s", MULTIPATH_CTL, strerror(errno));
  exit(1);
}

void remove_gnbd(char *name, int minor, int pid)
{
  int fd;

  if( (fd = open("/dev/gnbd_ctl", O_RDWR)) < 0){
    printe("cannot open /dev/gnbd_ctl : %s\n", strerror(errno));
    exit(1);
  }
  if (!override){
    if (ioctl(fd, GNBD_DISCONNECT, (unsigned long)minor) < 0 &&
        errno != ENOTCONN){
      printe("cannot disconnect device #%d : %s\n", minor, strerror(errno));
      exit(1);
    }
  }
  if (check_lock("gnbd_monitor.pid", NULL)){
    if (do_remove_monitored_dev(minor) < 0){
      if (!override)
        exit(1);
      printe("unable to stop monitoring device %s. removing anyway\n",
             name);
    }
  }
  if (pid > 0)
  	kill(pid, SIGKILL);
  cleanup_device(name, minor, fd);
  stop_mpath_monitoring(minor);
  close(fd);
}

void kill_gnbd_monitor(void){
  int pid;
  if (check_lock("gnbd_monitor.pid", &pid)){
    if (pid > 0){
      kill(pid, SIGKILL);
    }
  }
}

int device_active(int minor){
  list_t *item;
  gnbd_info_t *gnbd_info;
  char prefix[] = "/dev/gnbd/";
  char dev_path[256];
  
  list_foreach(item, &gnbd_list){
    struct stat statbuf;
    gnbd_info = list_entry(item, gnbd_info_t, list);
    if ( gnbd_info->minor_nr == minor ){
      strcpy(dev_path, prefix);
      strcat(dev_path, gnbd_info->name);
      
      if ( lstat(dev_path, &statbuf) == 0 ){
	if ( S_ISBLK(statbuf.st_mode) )
	  return 1; /* this is a valid block device */
      } else {
	if ( errno != ENOENT ){
	  printe("failed to stat device %s : %s\n", dev_path, strerror(errno));
	  return 1; /* if stat on the device failed for some reason*/
	}
      }
    }
  }
  return 0;
}

int uncached_imports_removed(){
  if (check_lock("gnbd_monitor.pid", NULL)){
    monitor_info_t *ptr, *devs;
    int i, count;
    
    if (do_list_monitored_devs(&devs, &count) < 0){
      printe("unable to get monitored device list : %s\n", gstrerror(errno));
      return 1;
    }
    if ( count <= 0 ){ /* monitoring nothing */
      free(devs);
      return 1;
    }
    
    ptr = devs;
    for (i = 0; i < count; i++, ptr++){
      if ( device_active(ptr->minor_nr) ){
	/* found an active device, bail */
	free(devs);
	return 0;
      }
    }
    free(devs);
    return 1; /* No active devices found */
  }
  return 0;
}

void update_devs(void){
  int minor;
  char path[LINE_MAX];
  gnbd_info_t *info;
  mode_t mode;

  for(minor = 0; minor < MAX_GNBD; minor++){
    if (match_info_minor(minor))
      continue;
    
    info = (gnbd_info_t *)malloc(sizeof(gnbd_info_t));
    if (!info){
      printe("couldn't allocated space for device #%d information : %s\n",
             minor, strerror(errno));
      exit(1);
    }
    memset(info, 0, sizeof(gnbd_info_t));
    
    info->minor_nr = minor;
    
    get_info(info);

    if (IS_UNUSED(info)){
      free(info);
      continue;
    }

    snprintf(path, LINE_MAX, "/dev/gnbd/%s", info->name);
    if (IS_READONLY(info))
      mode = S_IFBLK | S_IRUSR | S_IRGRP | S_IROTH;
    else
      mode = S_IFBLK | S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH;
    if (mknod(path, mode, makedev(gnbd_major, minor)) < 0){
      if (errno == EEXIST){
        if (override){
          remove_gnbd(NULL, minor, info->pid);
          printm("removed duplicate gnbd device %s\n", info->name);
          free(info);
	  if (uncached_imports_removed())
	    kill_gnbd_monitor();
	}
        else {
          printe("cannot create %s for gnbd device minor #%d, "
                 "name already used\n", path, minor);
          exit(1);
        }
      } else {
        printe("cannot create gnbd device file %s : %s\n", path,
               strerror(errno));
        exit(1);
      }
    } else{
      printm("created gnbd device file %s\n", path);
      info_add(info);
    }
  }
}

void check_files(void)
{
  DIR *dp;
  struct dirent *entry;
  int i;

  dp = opendir("/dev/gnbd/");
  if (dp == NULL){
    printe("unable to open directory /dev/gnbd : %s", strerror(errno));
    exit(1);
  }
  while( (entry = readdir(dp)) != NULL){
    if (strcmp(entry->d_name, ".") != 0 && strcmp(entry->d_name, "..") != 0)
      add_device(entry->d_name);
  }
  update_devs();
  closedir(dp);
  for (i = 0; i <= maxminor; i++)
    create_generic_gnbd_file(i);
}

void remove_all(void){
  list_t *item;
  gnbd_info_t *gnbd_info;
  
  list_foreach(item, &gnbd_list){
    gnbd_info = list_entry(item, gnbd_info_t, list);
    remove_gnbd(gnbd_info->name, gnbd_info->minor_nr, gnbd_info->pid);
  }

  if (uncached_imports_removed())
    kill_gnbd_monitor();
}

void remove_clients(char *names[], int num){
  list_t *item;
  gnbd_info_t *gnbd_info;
  int i;

  for (i = 0; i < num; i++){
    list_foreach(item, &gnbd_list){
      gnbd_info = list_entry(item, gnbd_info_t, list);
      if(strncmp(gnbd_info->name, names[i], 32) == 0){
        remove_gnbd(gnbd_info->name, gnbd_info->minor_nr, gnbd_info->pid);
        break;
      }
    }
    if (item == &gnbd_list){
      printe("cannot find device '%s'\n", names[i]);
      exit(1);
    }
  }

  if (uncached_imports_removed())
    kill_gnbd_monitor();
}

void fence(char *host, char *server, int is_fence)
{
  node_req_t node;
  int sock_fd;
  uint32_t msg;
  int err = 0;
  char *fence_str;

  if (is_fence)
    fence_str = "fence";
  else
    fence_str = "unfence";

  if (strlen(host) > 64){
    printe("name of node to %s must be <= 64 characters\n", fence_str);
    exit(1);
  }
  strncpy(node.node_name, host, 65);
  sock_fd = connect_to_server(server, (uint16_t)server_port);
  if (sock_fd < 0){
    printe("cannot connect to %s (%d) : %s\n", server, sock_fd, strerror(errno));
    exit(1);
  }
  if (is_fence)
    err = send_u32(sock_fd, EXTERN_FENCE_REQ);
  else
    err = send_u32(sock_fd, EXTERN_UNFENCE_REQ);
  if (err < 0){
    printe("cannot send %s request to server : %s\n", fence_str,
           gstrerror(errno));
    exit(SERVER_ERR);
  }
  if (retry_write(sock_fd, &node, sizeof(node)) < 0){
    printe("cannot send %s node to server : %s\n", fence_str,
           gstrerror(errno));
    exit(SERVER_ERR);
  }
  if (recv_u32(sock_fd, &msg) < 0){
    printe("cannot read %s reply from server : %s\n", fence_str,
           gstrerror(errno));
    exit(SERVER_ERR);
  }
  close(sock_fd);
  if (msg != EXTERN_SUCCESS_REPLY){
    printe("%s failed : %s\n", fence_str, strerror(msg));
    exit(SERVER_ERR);
  }
  printv("%s %sd\n", host, fence_str);
}

int start_receiver(int minor_nr)
{
  int ret;
  char cmd[256];

  snprintf(cmd, 256, "gnbd_recvd %s -d %d", ((is_clustered)? "" : "-n"), minor_nr);
  if( (ret = system(cmd)) < 0){
    printe("system() failed : %s\n", strerror(errno));
    return -1;
  }
  if (ret != 0){
    printe("gnbd_recvd failed\n");
    return -1;
  }
  return 0;
}

int restart_device(gnbd_info_t *gnbd){
  char filename[32];

  snprintf(filename, 32, "gnbd_recvd-%d.pid", gnbd->minor_nr);
  if (gnbd->connected > 0)
    return 0;
  if (check_lock(filename, NULL))
    return 0;
  /*
   * If the device is flushed, you need to wait for all the current
   * users to close it, before you reconnect
   */
  if (gnbd->pid < 0 && gnbd->usage > 0)
    return 0;
  if (start_receiver(gnbd->minor_nr) == 0){
    printm("restarted gnbd_recvd for gnbd device %s\n", gnbd->name);
    return 0;
  }
  printm("cannot restart server for gnbd device %s\n", gnbd->name);
  return -1;
}

void reimport_device(import_info_t *info, char *host,
                     gnbd_info_t *gnbd)
{
  if (check_lock("gnbd_monitor.pid", NULL)){
    monitor_info_t *ptr, *devs;
    int i, count;
    
    if (do_list_monitored_devs(&devs, &count) < 0){
      printe("unable to get monitored device list. cannot reimport %s\n",
             info->name);
      return;
    }
    ptr = devs;
    for (i = 0; i < count; i++, ptr++){
      if (ptr->minor_nr == gnbd->minor_nr){
        printe("cannot reimport monitored device %s\n", info->name);
        return;
      }
    }
    free(devs);
  }
  snprintf(sysfs_buf, 4096, "%s/%hx\n", host, server_port);
  if (__set_sysfs_attr(gnbd->minor_nr, "server", sysfs_buf) < 0){
    if (errno == EBUSY)
      printe("cannot reimport already connected device %s\n", info->name);
    return;
  }
  printm("reimporting device %s on server %s, port %u\n", gnbd->name,
         gnbd->server_name, gnbd->port);
  restart_device(gnbd);
}

int are_nodes_equal(char *node1, char *node2){
  struct addrinfo *ai1, *ai2;
  int ret = 0;
 
  ret = getaddrinfo(node1, NULL, NULL, &ai1);
  if (ret){
    printe("cannot get address info for %s : %s\n", node1,
           (ret == EAI_SYSTEM)? strerror(errno) : gai_strerror(ret));
    exit(1);
  }
  ret = getaddrinfo(node2, NULL, NULL, &ai2);
  if (ret){
    printe("cannot get address info for %s : %s\n", node2,
           (ret == EAI_SYSTEM)? strerror(errno) : gai_strerror(ret));
    exit(1);
  }
  ret = check_addr_info(ai1, ai2);

  freeaddrinfo(ai1);
  freeaddrinfo(ai2);
  return ret;
}    

int create_device(import_info_t *info, char *host)
{
  int minor;
  mode_t mode;
  char path[LINE_MAX];
  gnbd_info_t *entry;
  
  entry = match_info_name(info->name);
  if (entry){
    printv("There is already a GNBD with the name %s.", info->name);
    if (server_port == entry->port &&
        are_nodes_equal(host, entry->server_name)){
      restart_device(entry);
      return -1;
    }
    if (info->timeout){
      printe("cannot reimport uncached GNBD %s\n", info->name);
      return -1;
    }
    if (entry->pid < 0 && entry->usage > 0){
      printe("Cannot reimport GNBD %s until all users have closed it\n",
             info->name);
      return -1;
    }
    reimport_device(info, host, entry);
    return -1;
  }
  minor = find_empty_minor();
  set_sysfs_attr(minor, "name", info->name);
  snprintf(sysfs_buf, 4096, "0x%hx", info->flags);
  set_sysfs_attr(minor, "flags", sysfs_buf);
  snprintf(sysfs_buf, 4096, "%s/%hx\n", host, server_port);
  set_sysfs_attr(minor, "server", sysfs_buf);
  if (info->flags & GNBD_READ_ONLY)
    mode = S_IFBLK | S_IRUSR | S_IRGRP | S_IROTH;
  else
    mode = S_IFBLK | S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH;
  snprintf(path, LINE_MAX, "/dev/gnbd/%s", info->name);
  if (mknod(path, mode, makedev(gnbd_major, minor)) < 0 &&
      errno != EEXIST){
    printe("could not create block device file for %s : %s\n",
	   info->name, strerror(errno));
    exit(1);
  }
  if (minor > maxminor){
    maxminor = minor;
    create_generic_gnbd_file(maxminor);
  }
  printm("created gnbd device %s\n", info->name);
  return minor;
}

void setclients(char *host)
{
  char *buf;
  import_info_t *ptr;
  int size;
  int minor_nr;
  size = read_from_server(host, EXTERN_NAMES_REQ, &buf);
  if (!size)
    return;
  ptr = (import_info_t *)buf;
  while ((char *)ptr < buf + size){
    if (ptr->timeout != 0 && is_clustered == 0){
      printe("cannot import uncached devices while using the -n option\n" MAN_MSG);
      exit(1);
    }
    minor_nr = create_device(ptr, host);
    if (minor_nr >= 0){
      if (start_gnbd_monitor(minor_nr, (int)ptr->timeout, host) < 0)
        exit(1);
      if (start_receiver(minor_nr))
        exit(1);
      do_set_sysfs_attr(minor_nr, "block/uevent", "1");
    }
    ptr++;
  }
}

void get_serv_list(char *host)
{
  char *buf;
  import_info_t *ptr;
  int size;
  size = read_from_server(host, EXTERN_NAMES_REQ, &buf);
  if (size) {
    ptr = (import_info_t *)buf;
    while ((char *)ptr < buf + size){
      printf("%s\n", ptr->name);
      ptr++;
    }
  }
  printf("\n");
  free(buf);
}

void get_banned_list(char *host)
{
  char *buf;
  node_req_t *ptr;
  int size;
  size = read_from_server(host, EXTERN_LIST_BANNED_REQ, &buf);
  if (size) {
    ptr = (node_req_t *)buf;
    while ((char *)ptr < buf + size){
      printf("%s\n", ptr->node_name);
      ptr++;
    }
  }
  printf("\n");
  free(buf);
}

void list(void){
  list_t *item;
  gnbd_info_t *info;

  if (verbosity == QUIET)
    return;
  list_foreach(item, &gnbd_list){
    info = list_entry(item, gnbd_info_t, list);
    printf("Device name : %s\n"
           "----------------------\n"
           "    Minor # : %d\n"
           " sysfs name : /block/gnbd%d\n"
           "     Server : %s\n"
           "       Port : %d\n"
           "      State : %s %s %s\n"
           "   Readonly : %s\n"
           "    Sectors : %llu\n\n",
           info->name,
           info->minor_nr,
           info->minor_nr,
           info->server_name,
           info->port,
           (info->usage > 0)? "Open" : "Close",
           (info->connected > 0)? "Connected" : "Disconnected",
           (info->waittime != -1)? "Pending" : "Clear",
           IS_READONLY(info)? "Yes" : "No",
           (unsigned long long)info->sectors);
  }
}

void get_uid(char *name){
  list_t *item;
  gnbd_info_t *gnbd = NULL;
  char *uid;
  device_req_t req;
  int minor_nr = -1;

  sscanf(name, "/block/gnbd%d", &minor_nr);
  list_foreach(item, &gnbd_list){
    gnbd = list_entry(item, gnbd_info_t, list);
    if (minor_nr >= 0 && minor_nr == gnbd->minor_nr)
      break;
    if (strncmp(gnbd->name, name, 32) == 0)
      break;
    gnbd = NULL;
  }
  if (!gnbd){
    printe("cannot find device '%s'\n", name);
    exit(1);
  }
  strncpy(req.name, gnbd->name, 32);
  req.name[31] = 0;
  talk_to_server(gnbd->server_name, EXTERN_UID_REQ, &uid, &req, sizeof(req));
  if (uid)
  	printf("%s\n", uid);
  free(uid);
}
  
void validate_gnbds(void){
  list_t *item;
  char filename[32];
  gnbd_info_t *gnbd;
  
  list_foreach(item, &gnbd_list){
    gnbd = list_entry(item, gnbd_info_t, list);    
    snprintf(filename, 32, "gnbd_recvd-%d.pid", gnbd->minor_nr);
    if (restart_device(gnbd) == 0)
      continue;
    if (override == 1){
      int fd;
      printm("removing device\n");
      if( (fd = open("/dev/gnbd_ctl", O_RDWR)) < 0){
        printe("cannot open /dev/gnbd_ctl : %s\n", strerror(errno));
        exit(1);
      }
      if (check_lock("gnbd_monitor.pid", NULL)){
        if (do_remove_monitored_dev(gnbd->minor_nr) < 0){
          printe("unable to stop monitoring device %s. removing anyway\n",
                 gnbd->name);
        }
      }
      cleanup_device(gnbd->name, gnbd->minor_nr, fd);
      close(fd);
    }
  }
}

int main(int argc, char **argv)
{
  int action = 0;
  char *host = NULL;
  char *fence_server = NULL;
  char *dev = NULL;
  int c;

  list_init(&gnbd_list);
  program_name = "gnbd_import";
  while( (c = getopt(argc, argv, "ac:e:hi:lnOp:qRrs:t:U:u:Vv")) != -1){
    switch(c){
    case ':':
    case '?':
      fprintf(stderr, "Please use '-h' for usage.\n");
      return 1;
    case 'a':
      set_action(ACTION_VALIDATE);
      continue;
    case 'c':
      set_action(ACTION_LIST_BANNED);
      host = optarg;
      continue;
    case 'e':
      set_action(ACTION_LIST_EXPORTED);
      host = optarg;
      continue;
    case 'h':
      return usage();
    case 'i':
      set_action(ACTION_IMPORT);
      host = optarg;
      continue;
    case 'k':
      set_action(ACTION_FAIL_SERVER);
      host = optarg;
      continue;
    case 'l':
      set_action(ACTION_LIST);
      continue;
    case 'n':
      is_clustered = 0;
      continue;
    case 'O':
      override = 1;
      continue;
    case 'p':
      if (sscanf(optarg, "%u", &server_port) != 1){
        printe("invalid port number, %s\n" MAN_MSG, optarg);
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
    case 'R':
      set_action(ACTION_REMOVE_ALL);
      continue;
    case 'r':
      set_action(ACTION_REMOVE);
      continue;
    case 's':
      set_action(ACTION_FENCE);
      host = optarg;
      continue;
    case 't':
      fence_server = optarg;
      continue;
    case 'U':
      set_action(ACTION_GET_UID);
      dev = optarg;
      continue;
    case 'u':
      set_action(ACTION_UNFENCE);
      host = optarg;
      continue;
    case 'V':
      printf("%s %s (built %s %s)\n", argv[0],
             RELEASE_VERSION, __DATE__, __TIME__);
      printf("%s\n", REDHAT_COPYRIGHT);
      return 0;
    case 'v':
      if (verbosity == QUIET){
        printe("cannot use both -q and -v options\nPlease use '-h' for usage.\n");
        exit(1);
      }
      verbosity = VERBOSE;
      continue;
    default:
      printe("invalid option -- %c\n", c);
      fprintf(stderr, "Please use '-h' for usage.\n");
      exit(1);
    }
  }
  if (!action)
    action = ACTION_LIST;
  if (get_my_nodename(node_name, is_clustered) < 0){
    printe("cannot get node name : %s\n", strerror(errno));
    if (is_clustered){
	if (errno == ESRCH)
	  printe("No cluster manager is running\n");
        if (errno == ELIBACC)
          printe("cannot find magma plugins\n");
	printe("If you are not planning to use a cluster manager, use -n\n");
    }
    return 1;
  }
  if (fence_server && action != ACTION_FENCE && action != ACTION_UNFENCE){
    printe("-t can only be used with -s or -u\n" MAN_MSG);
    return 1;
  }
  if ((action == ACTION_FENCE || action == ACTION_UNFENCE) &&
      !strcmp(host, "localhost")){
    host = node_name;
  }
  if (argc != optind && action != ACTION_REMOVE){
    printe("extra operand for action: %s\n", argv[optind]);
    fprintf(stderr, "please use '-h' for usage.\n");
    return 1;
  }
  if (action != ACTION_LIST_EXPORTED && action != ACTION_LIST_BANNED &&
      action != ACTION_FENCE && action != ACTION_UNFENCE){
    gnbd_major = get_major_nr();
    check_gnbd_ctl();
    create_gnbd_dir();
    check_files();
  }
  switch(action){
  case ACTION_FENCE:
  case ACTION_UNFENCE:
    if (!fence_server){
      printe("you must use -t with %s\n" MAN_MSG,
             (action == ACTION_FENCE)? "-s" : "-u");
      return 1;
    }
    fence(host, fence_server, (action == ACTION_FENCE)? 1 : 0);
    return 0;
  case ACTION_REMOVE:
    if (argc == optind){
      printe("missing operand for remove action\n");
      fprintf(stderr, "please use '-h' for usage.\n");
      return 1;
    }
    remove_clients(argv + optind, argc - optind);
    return 0;
  case ACTION_REMOVE_ALL:
    remove_all();
    return 0;
  case ACTION_VALIDATE:
    validate_gnbds();
    return 0;
  case ACTION_LIST_EXPORTED:
    get_serv_list(host);
    return 0;
  case ACTION_IMPORT:
    setclients(host);
    return 0;
  case ACTION_LIST:
    list();
    return 0;
  case ACTION_LIST_BANNED:
    get_banned_list(host);
    return 0;
  case ACTION_GET_UID:
    get_uid(dev);
    return 0;
  default:
    printe("unknown action %d\n", action);
    return 1;
  }
}
