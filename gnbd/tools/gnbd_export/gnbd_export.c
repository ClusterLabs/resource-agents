#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <arpa/inet.h>
#include <dirent.h>
#include <limits.h>
#include <ctype.h>
#define _GNU_SOURCE
#include <getopt.h>

#include "global.h"
#include "gnbd_endian.h"
#include "gnbd_utils.h"
#include "local_req.h"
#include "device.h"
#include "trans.h"

#include "copyright.cf"

#define TIMEOUT_DEFAULT 60

#define MAN_MSG "Please see man page for details.\n"

int start_gnbd_clusterd(void)
{
  int ret;

  if( (ret = system("gnbd_clusterd")) < 0){
    printe("system() failed. canot start gnbd_clusterd : %s\n",
           strerror(errno));
    return -1;
  }
  if (ret != 0){
    printe("gnbd_clusterd failed\n");
    return -1;
  }
  return 0;
}

void stop_gnbd_clusterd(void)
{
  int ret;

  if( (ret = system("gnbd_clusterd -k")) < 0){
    printe("system() failed. cannot stop gnbd_clusterd : %s\n",
           strerror(errno));
    return;
  }
  if (ret != 0){
    printe("stopping gnbd_clusterd failed\n");
    return;
  }
}

int servcreate(char *name, char *device, uint32_t timeout, uint8_t readonly,
               char *uid){
  info_req_t create_req;
  int fd;
  
  if (timeout && start_gnbd_clusterd())
    return 1;

  strncpy(create_req.name, name, 32);
  create_req.name[31] = 0;
  if (strchr(create_req.name, '/')){
    printe("server name %s is invalid. Names cannot contain a '/'\n",
           create_req.name);
    return 1;
  }
  strncpy(create_req.path, device, 1024);
  create_req.path[1023] = 0;
  if (uid){
    strncpy(create_req.uid, uid, 64);
    create_req.uid[63] = 0;
  }
  else
    create_req.uid[0] = 0;
  create_req.timeout = timeout;
  create_req.flags = (((readonly)? GNBD_FLAGS_READONLY : 0) |
                      ((timeout)? GNBD_FLAGS_UNCACHED : 0));
  fd = connect_to_comm_device("gnbd_serv");
  if (fd < 0)
    return 1;
  if (send_cmd(fd, LOCAL_CREATE_REQ, "create") < 0)
    return 1;
  if (write(fd, &create_req, sizeof(create_req)) != sizeof(create_req)){
    printe("sending create data failed : %s\n", strerror(errno));
    return 1;
  }
  if (recv_reply(fd, "create") < 0)
    return 1;
  printm("created GNBD %s serving file %s\n", name, device);
  close(fd);
  return 0;
}

void invalidate_serv(char *name)
{
  name_req_t invalidate_req;
  int fd;
  
  strncpy(invalidate_req.name, name, 32);
  invalidate_req.name[31] = 0;
  fd = connect_to_comm_device("gnbd_serv");
  if (fd < 0)
    exit(1);
  if (send_cmd(fd, LOCAL_INVALIDATE_REQ, "invalidate") < 0)
    exit(1);
  if (retry_write(fd, &invalidate_req, sizeof(invalidate_req)) < 0){
    printe("sending invalidate request data failed : %s\n", gstrerror(errno));
    exit(1);
  }
  if (recv_reply(fd, "invalidate") < 0)
    exit(1);
  close(fd);
}

void servremove(char *name)
{
  name_req_t remove_req;
  uint32_t reply;
  int fd;

  strncpy(remove_req.name, name, 32);
  remove_req.name[31] = 0;
  fd = connect_to_comm_device("gnbd_serv");
  if (fd < 0)
    exit(1);
  if (send_cmd(fd, LOCAL_REMOVE_REQ, "remove") < 0)
    exit(1);
  if (retry_write(fd, &remove_req, sizeof(remove_req)) < 0){
    printe("sending remove req failed: %s\n", gstrerror(errno));
    exit(1);
  }
  if (retry_read(fd, &reply, sizeof(reply)) < 0){
    printe("reading remove reply failed : %s\n", gstrerror(errno));
    exit(1);
  }
  if (reply && reply != LOCAL_RM_CLUSTER_REPLY){
    printe("remove request failed : %s\n", strerror(reply));
    exit(1);
  }
  if (reply == LOCAL_RM_CLUSTER_REPLY)
    stop_gnbd_clusterd();
  printm("removed GNBD %s\n", name);
  close(fd);
}

int validate(void)
{
  int fd;
  
  fd = connect_to_comm_device("gnbd_serv");
  if (fd < 0)
    return 1;
  if (send_cmd(fd, LOCAL_VALIDATE_REQ, "validate") < 0)
    return 1;
  if (recv_reply(fd, "validate") < 0)
    return 1;
  printm("removed invalid server processes\n");
  return 0;
}
  

int get_list_info(void **buffer, int *buffer_size, int cmd){
  void *buf;
  uint32_t size;
  int n, total;
  int fd;

  *buffer = NULL;
  *buffer_size = 0;
  
  fd = connect_to_comm_device("gnbd_serv");
  if (fd < 0)
    return -1;
  if (send_cmd(fd, cmd, "list") < 0)
    return -1;
  if (recv_reply(fd, "list") < 0)
    return -1;
  if (read(fd, &size, sizeof(size)) != sizeof(size)){
    printe("receiving list size failed : %s\n", strerror(errno));
    return -1;
  }
  if (size == 0){
    *buffer_size = size;
    close(fd);
    return 0;
  }
  buf = malloc(size);
  if (!buf){
    printe("couldn't allocate memory for list : %s\n", strerror(errno));
    return -1;
  }
  total = 0;
  while(total < size){
    n = read(fd, buf + total, size - total);
    if (n <= 0){
      printe("receiving list failed : %s\n", strerror(errno));
      free(buf);
      return -1;
    }
    total += n;
  }

  *buffer = buf;
  *buffer_size = size;
  close(fd);
  return 0;
}


int removeall(int force){
  int size;
  void *buf;
  info_req_t *info;

  if (get_list_info(&buf, &size, LOCAL_FULL_LIST_REQ) < 0)
    return 1;
  if (size == 0)
    return 0;
  info = (info_req_t *)buf;
  while ((void *)info < buf + size){
    if (force)
      invalidate_serv(info->name);
    servremove(info->name);
    info++;
  }
  free(buf);
  return 0;
}

int gserv_list(void){
  int size;
  void *buf;
  gserv_req_t *info;
 
  if (get_list_info(&buf, &size, LOCAL_GSERV_LIST_REQ) < 0)
    return 1;
  if (verbosity == QUIET){
    if (size)
      free(buf);
    return 0;
  }
  if (size == 0){
    printf("no server processes\n");
    return 0;
  }
  info = (gserv_req_t *)buf;
  printf("  pid       client      device\n");
  printf("--------------------------------\n");
  while((void *)info < buf + size){
    printf("%5d  %15s  %s\n", info->pid, info->node,
           info->name);
    info++;
  }
  free(buf);
  return 0;
}

int list(void){
  int size;
  void *buf;
  info_req_t *info;
  int i = 0;
  
  if (get_list_info(&buf, &size, LOCAL_FULL_LIST_REQ) < 0)
    return 1;
  if (verbosity == QUIET){
    if (size)
      free(buf);
    return 0;
  }
  if (size == 0){
    printf("no exported GNBDs\n");
    return 0;
  }
  info = (info_req_t *)buf;
  while ((void *)info < buf + size){
    i++;
    printf("Server[%d] : %s %s\n"
           "--------------------------\n"
           "      file : %s\n"
           "   sectors : %Lu\n"
           "  readonly : %s\n"
           "    cached : %s\n",
           i, info->name, (info->flags & GNBD_FLAGS_INVALID)? "(invalid)" : "",
           info->path, (long long unsigned int)info->sectors,
           (info->flags & GNBD_FLAGS_READONLY)? "yes" : "no",
           (info->flags & GNBD_FLAGS_UNCACHED)? "no" : "yes");
    if (info->timeout)
      printf("   timeout : %u\n", info->timeout);
    else
      printf("   timeout : no\n");
    if (info->uid[0] != '\0')
      printf("       uid : %s\n", info->uid);
    printf("\n");
    info++;
  }
  free(buf);
  return 0;
}


void get_dev(char *path, int *major, int *minor)
{
  struct stat stat_buf;
  *major = -1;
  *minor = -1;

  if (stat(path, &stat_buf) < 0){
    printe("cannot stat %s : %s\n", path, strerror(errno));
    exit(1);
  }
  if (!S_ISBLK(stat_buf.st_mode)){
    printe("path '%s' is not a block device. cannot get uid information\n",
           path);
    exit(1);
  }
  *major = major(stat_buf.st_rdev);
  *minor = minor(stat_buf.st_rdev);
}

char *get_sysfs_info(char *path) {
  int fd;
  int bytes;
  int count = 0;

  if ((fd = open(path, O_RDONLY)) < 0) {
    printe("cannot open %s : %s\n", path, strerror(errno));
    exit(1);
  }
  while (count < 4096) {
    bytes = read(fd, &sysfs_buf[count], 4096 - count);
    if (bytes < 0 && errno != EINTR) {
      printe("cannot read from %s : %s\n", path, strerror(errno));
      exit(1);
    }
    if (bytes == 0)
      break;
    count += bytes;
  }
  if (sysfs_buf[count - 1] == '\n' || count == 4096)
    sysfs_buf[count - 1] = '\0';
  else
    sysfs_buf[count] = '\0';
  close(fd);
  return sysfs_buf;
}
    
#define SYSFS_PATH_MAX 64
#define SYSFS_PATH_BASE "/sys/block"
#define SYSFS_PATH_BASE_SIZE 10
int get_sysfs_majmin(char *dev, int *major, int *minor)
{
  char path[SYSFS_PATH_MAX];
  char *buf;

  if (snprintf(path, SYSFS_PATH_MAX, "%s/%s/dev", SYSFS_PATH_BASE, dev) >=
      SYSFS_PATH_MAX) {
    printe("sysfs path name '%s/%s/dev' too long\n", SYSFS_PATH_BASE, dev);
    exit(1);
  }
  buf = get_sysfs_info(path);
  if (sscanf(buf, "%u:%u", major, minor) != 2){
    printe("cannot parse %s entry '%s'\n", path, buf);
    exit(1);
  }
  return 0;
}

int get_sysfs_range(char *dev, int *range)
{
  char path[SYSFS_PATH_MAX];
  char *buf;

  if (snprintf(path, SYSFS_PATH_MAX, "%s/%s/range", SYSFS_PATH_BASE, dev) >=
      SYSFS_PATH_MAX) {
    printe("sysfs path name '%s/%s/range' too long\n", SYSFS_PATH_BASE, dev);
    exit(1);
  }
  buf = get_sysfs_info(path);
  if (sscanf(buf, "%u", range) != 1){
    printe("cannot parse %s etnry '%s'\n", path, buf);
    exit(1);
  }
  return 0;
}

char *get_sysfs_name(int major, int minor){
  char *name = NULL;
  DIR *dir;
  struct dirent *dp;

  dir = opendir(SYSFS_PATH_BASE);
  if (!dir) {
    printe("cannot open %s to find the device name for %d:%d : %s\n",
           SYSFS_PATH_BASE, major, minor, strerror(errno));
    exit(1);
  }
  while ((dp = readdir(dir))) {
    int dev_major, dev_minor, dev_range;
    if (!strcmp(dp->d_name, ".") || !strcmp(dp->d_name, ".."))
      continue;
    get_sysfs_majmin(dp->d_name, &dev_major, &dev_minor);
    get_sysfs_range(dp->d_name, &dev_range);
    if (major == dev_major && minor >= dev_minor &&
        minor < dev_minor + dev_range){
      if (minor == dev_minor)
        name = strdup(dp->d_name);
      else {
        name = malloc(SYSFS_PATH_MAX);
        if (!name){
          printe("cannot allocate memory for sysfs name : %s\n",
                 strerror(errno));
          exit(1);
        }
        sprintf(name, "%s/%s%d", dp->d_name, dp->d_name, minor - dev_minor);
      }
      break;
    }
  }
  if (closedir(dir) < 0){
    printe("cannot close dir %s : %s\n", SYSFS_PATH_BASE, strerror(errno));
    exit(1);
  }
  if (!name) {
    printe("cannot find sysfs block device %d:%d\n", major, minor);
    exit(1);
  }
  return name;
}

size_t read_all(int fd, void *buf, size_t len)
{
  size_t total = 0;

  while (len) {
    ssize_t n = read(fd, buf, len);
    if (n < 0) {
      if ((errno == EINTR) || (errno == EAGAIN))
        continue;
      if (!total)
        return -1;
      return total;
    }
    if (!n)
      return total;
    buf = n + (char *)buf;
    len -= n;
    total += n;
  }
  return total;
}


char *execute_uid_program(char *command){
  char *uid;
  char **argv = NULL;
  char *ptr = command;
  int fds[2], size = 0;
  char *save = strdup(command);
  pid_t pid;
  int val, status, count;

  uid = malloc(sizeof(char) * 64);
  if (!uid){
    printe("cannot allocate memory for uid\n");
    exit(1);
  }
  while (*ptr){
    char *delim = "\t\n\r\t\v ";
    int quote = 0;
    while(isspace(*ptr))
      ptr++;
    if (!*ptr)
      break;
    if (*ptr == '\''){
      quote = 1;
      ptr++;
      delim = "'";
    }
    argv = realloc(argv, (size + 2) * sizeof(char **));
    if (!argv){
      printe("cannot allocate memory for command line\n");
      exit(1);
    }
    argv[size++] = ptr;
    ptr = strpbrk(ptr, delim);
    if (!ptr){
      if (quote){
        printe("invalid get_uid command (%s) non terminated quote\n", save);
        exit(1);
      }
      break;
    }
    *ptr++ = 0;
  }
  if (!size){
    printe("invalid get_uid command (%s) empty command\n", save);
    exit(1);
  }      
  argv[size] = NULL;
 
  if (pipe(fds) < 0){
    printe("couldn't open a pipe for get_uid command : %s\n", strerror(errno));
    exit(1);
  }
  pid = fork();
  if (pid < 0){
    printe("couldn't fork get_uid command : %s\n", strerror(errno));
    exit(1);
  }
  if (!pid){
    close(STDOUT_FILENO);
    dup(fds[1]);
    execv(argv[0], argv);
    printe("couldn't exec '%s' : %s\n", argv[0], strerror(errno));
    exit(1);
  }
  close(fds[1]);

  val = fcntl(0, F_GETFL, 0);
  if (val >= 0){
    val |= O_NONBLOCK;
    fcntl(0, F_SETFL, val);
  }

  waitpid(pid, &status, 0);
  if ((count = read_all(fds[0], uid, 63)) < 0){
    printe("couldn't read from get_uid command : %s\n", strerror(errno));
    exit(1);
  }
  if (count && uid[count-1] == '\n')
    count--;
  uid[count] = 0;
  close(fds[0]);

  if (!WIFEXITED(status)){
    printe("get_uid command '%s' exitted abnormally (%s)\n", argv[0], uid);
    exit(1);
  }
  status = WEXITSTATUS(status);
  if (status != 0){
    printe("get_uid command '%s' failed: %d (%s)\n", argv[0], status, uid);
    exit(1);
  }
  return uid;
}

#define SPACE_LEFT (&command[LINE_MAX - 1] - dest)
char *get_uid(char *format, char *path)
{
  char temp[24];
  char command[LINE_MAX];
  int escape, major, minor;
  char *name, *src, *dest;

  src = format;
  dest = command;
  escape = 0;
  major = -1;
  minor = -1;
  name = NULL;

  while(*src){
    if (!SPACE_LEFT){
      *dest = 0;
      printe("get_uid command string '%s' too long\n", command);
      exit(1);
    }
    if (escape){
      int len;
      switch(*src){
      case 'M':
        if (major == -1)
          get_dev(path, &major, &minor);
        sprintf(temp, "%d", major);
        len = strlen(temp);
        if (len > SPACE_LEFT){
          printe("get_uid command string '%s' too long\n", command);
          exit(1);
        }
        strncpy(dest, temp, len);
        dest += len;
        break;
      case 'm':
        if (minor == -1)
          get_dev(path, &major, &minor);
        sprintf(temp, "%d", minor);
        len = strlen(temp);
        if (len > SPACE_LEFT){
          *dest = 0;
          printe("get_uid command string '%s' too long\n", command);
          exit(1);
        }
        strncpy(dest, temp, len);
        dest += len;
        break;
      case 'n':
        if (!name){
          if (major == -1)
            get_dev(path, &major, &minor);
          name = get_sysfs_name(major, minor);
        }
        len = strlen(name);
        if (len > SPACE_LEFT){
           *dest = 0;
           printe("get_uid command string '%s' too long\n", command);
           exit(1);
        }
        strncpy(dest, name, len);
        dest += len;
        break;
      default:
        if (SPACE_LEFT < 2){
          *dest = 0;
          printe("get_uid command string '%s' too long\n", command);
          exit(1);
        }
        *dest++ = '%';
        *dest++ = *src;
      }
      escape = 0;
    }
    else if (*src == '%')
      escape = 1;
    else{
      *dest = *src;
      dest++;
    }
    src++;
  }
  *dest = 0;
  return execute_uid_program(command);
}

int usage(void){
  printf(
"Usage:\n"
"\n"
"gnbd_export [options]\n"
"\n"
"Options:\n"
"  -a               validate the servers, and remove the bad ones\n"
"  -c               enable caching (used with -e)\n"
"  -d <device>      the device to create a GNBD on\n"
"  -e <GNBD>        export the specified GNBD\n"
"  -h               print this help message\n"
"  -l               list the exported GNBDS (default)\n"
"  -L               list the server processes\n"
"  -O               Force unexport. (used with -r and -R)\n"
"  -o               export device readonly (used with -e)\n"
"  -q               quiet mode\n"
"  -R               unexport all GNBDs\n"
"  -r [GNBD | list] unexport the specified GNBD(s)\n"
"  -t <seconds>     set the timeout duration\n"
"  -u <uid>         manually set the Unique ID of a device (used with -e)\n"
"  -U[command]      command to get the Unique ID of a device (used with -e)\n"
"                   If no command is specificed, the default is\n"
"                   \""DEFAULT_GETUID" %%n\"\n"
"  -v               verbose output (useful with -l)\n"
"  -V               version information\n");
  return 0;
}


#define ACTION_EXPORT     1
#define ACTION_REMOVE     2
#define ACTION_LIST       3
#define ACTION_REMOVE_ALL 4
#define ACTION_GSERV_LIST 5
#define ACTION_VALIDATE   6

char action_to_flag(int action){
  switch(action){
  case ACTION_EXPORT:
    return 'e';
  case ACTION_REMOVE:
    return 'r';
  case ACTION_LIST:
    return 'l';
  case ACTION_REMOVE_ALL:
    return 'R';
  case ACTION_GSERV_LIST:
    return 'L';
  case ACTION_VALIDATE:
    return 'a';
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

int main(int argc, char **argv){
  int c, i;
  int action = 0;
  int cached = 0;
  unsigned int timeout = 0;
  int force = 0;
  int readonly = 0;
  char *device = NULL;
  char *gnbd_name = NULL;
  char *uid = NULL;
  char *uid_program = NULL;

  program_name = "gnbd_export";
  while ((c = getopt(argc, argv, "acd:e:hlLOoqrRt:u:U::vV")) != -1){
    switch(c){
    case ':':
    case '?':
      fprintf(stderr, "Please use '-h' for usage.\n");
      return 1;
    case 'a':
      set_action(ACTION_VALIDATE);
      continue;
    case 'c':
      cached = 1;
      continue;
    case 'd':
      device = optarg;
      continue;
    case 'e':
      set_action(ACTION_EXPORT);
      gnbd_name = optarg;
      continue;
    case 'h':
      return usage();
    case 'l':
      set_action(ACTION_LIST);
      continue;
    case 'L':
      set_action(ACTION_GSERV_LIST);
      continue;
    case 'O':
      force = 1;
      continue;
    case 'o':
      readonly = 1;
      continue;
    case 'q':
      verbosity = QUIET;
      continue;
    case 'r':
      set_action(ACTION_REMOVE);
      continue;
    case 'R':
      set_action(ACTION_REMOVE_ALL);
      continue;
    case 't':
      if (sscanf(optarg, "%u", &timeout) != 1 || timeout == 0){
        printe("invalid timeout '%s' with -t\n" MAN_MSG, optarg);
        return 1;
      }
      continue;
    case 'u':
      uid = optarg;
      continue;
    case 'U':
      uid_program = optarg;
      if (!uid_program)
        uid_program = DEFAULT_GETUID;
      continue;
    case 'v':
      verbosity = VERBOSE;
      continue;
    case 'V':
      printf("%s %s (built %s %s)\n", argv[0],
             RELEASE_VERSION, __DATE__, __TIME__);
      printf("%s\n", REDHAT_COPYRIGHT);
      return 0;
    default:
      printe("invalid option -- %c\nPlease use '-h' for usage.\n", c);
      return 1;
    }
  }
  if (cached && (uid || uid_program)){
    printe("the -c option may not be used with the -u or -U option\n" MAN_MSG);
    return 1;
  }
  if (timeout && cached){
    printe("the -t option may not be used with the -c option\n" MAN_MSG);
    return 1;
  }
  if ((cached || timeout || device || readonly || uid || uid_program) &&
      action != ACTION_EXPORT){
    printe("the -c, -t, -d , -u and -U flags may only be used with -e\n"
           MAN_MSG);
    return 1;
  }
  if (force && action != ACTION_REMOVE && action != ACTION_REMOVE_ALL){
    printe("the -O option mhy only be used with -r or -R.\n" MAN_MSG);
    return 1;
  }
  if (action != ACTION_REMOVE && optind != argc){
    printe("extra operand for action: %s\n", argv[optind]);
    fprintf(stderr, "please use '-h' for usage.\n");
    return 1;
  }
  switch(action){
  case ACTION_EXPORT:
    if (!device){
      printe("The -d option must be specified with -e.\n" MAN_MSG);
      return 1;
    }
    if (cached == 0 && timeout == 0)
      timeout = TIMEOUT_DEFAULT;
    if (uid && uid_program){
      printe("the -u and -U options cannot be used together.\n" MAN_MSG);
      return 1;
    }
    if (uid_program)
      uid = get_uid(uid_program, device); 
    return servcreate(gnbd_name, device, (uint32_t)timeout, (uint8_t)readonly,
                      uid);
  case ACTION_REMOVE:
    if (optind == argc){
      printe("missing operand for remove action\n");
      fprintf(stderr, "please use '-h' for usage.\n");
      return 1;
    }
    for (i = optind; i < argc; i++){
      if (force)
        invalidate_serv(argv[i]);
      servremove(argv[i]);
    }
    return 0;
  case ACTION_LIST: case 0:
    return list();
  case ACTION_REMOVE_ALL:
    return removeall(force);
  case ACTION_GSERV_LIST:
    return gserv_list();
  case ACTION_VALIDATE:
    return validate();
  default:
    printe("unrecognized action value\n");
    return 1;
  }
}
