#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include <ctype.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/wait.h>

#include "fence_return.h"
#include "copyright.cf"

/* name of the command that is needed to do the gnbd fencing */
#define GNBD_FENCE_CMD "gnbd_import"
#define NODE_FENCE_CMD "fence_node"

char *pname = "fence_gnbd";

char arg[256];
char name[256];
char ipaddr[256];

int quiet_flag = 0;
int multipath = 0;

struct serv_list{
  char name[100];
  struct serv_list *next;
};

typedef struct serv_list serv_list_t;

#define printe(fmt, args...) \
{\
  if (!quiet_flag) \
    printf(fmt, ##args); \
}

#define fail(fmt, args...) \
{\
  if (!quiet_flag) \
    printf("failed: " fmt, ##args); \
  exit(1); \
}

serv_list_t *servers = NULL;
int num_servers = 0;
unsigned int wait_time = 2;
unsigned int retrys = 3;

void print_usage()
{
  printf("Usage:\n");
  printf("\n");
  printf("%s [options]\n\n", pname);
  printf("Options:\n");
  printf("  -h               Usage\n");
  printf("  -m               use multipath style fencing\n");
  printf("  -q               Quiet output\n");
  printf("  -s <node>        machine to fence\n");
  printf("  -t <node>        server to fence machine from\n");
  printf("  -V               Version information\n");
}

void parse_servers(char *str){
  char *end, *ptr;
  serv_list_t *tmp;
  
  end = str + strlen(str);
  while (str < end){
    if (isspace(*str)){
      str++;
      continue;
    }
    if (*str == '\'' || *str == '\"'){
      char match = *str;
      str++;
      ptr = strchr(str, match);
      if (ptr == NULL)
        fail("no closing for quote character %c in %s\n", match, str);
      *ptr = 0;
    }
    else{
      char *match = " \f\n\r\t\v";
      ptr = strpbrk(str, match);
      if (ptr != NULL)
        *ptr = 0;
      else
        ptr = end;
    }
    tmp = malloc(sizeof(serv_list_t));
    if (!tmp)
      fail("couldn't allocate memory for server list\n");
    strcpy(tmp->name, str);
    tmp->next = servers;
    servers = tmp;
    num_servers++;
    str = ptr + 1;
  }
}
    

void get_options(int argc, char **argv)
{
  int c;
  char *value;
 
  if (argc > 1)
  {
    serv_list_t *tmp;
    while ((c = getopt(argc, argv, "Vhmqs:t:")) != -1)
    {
      switch(c)
      {
      case 'V':
        printf("%s %s (built %s %s)\n", pname, RELEASE_VERSION,
                __DATE__, __TIME__);
        printf("%s\n", REDHAT_COPYRIGHT);
        exit(0);
        break;
        
      case 'h':
        print_usage();
        exit(0);

      case 'm':
        multipath = 1;
        break;

      case 'q':
        quiet_flag = 1;
        break;
        
      case 's':
        strcpy(ipaddr, optarg);
        break;

      case 't':
        tmp = malloc(sizeof(serv_list_t));
        if (!tmp)
          fail("couldn't allocate memory for server list.\n");
        strcpy(tmp->name, optarg);
        tmp->next = servers;
        servers = tmp;
        num_servers++;
        break;

      case ':':
      case '?':
        /* getopt prints an error msg to stderr, so we don't have to */
        fprintf(stderr, "Please use '-h' for usage.\n");
        exit(1);

      default:
        fail("invalid flag '-%c'\n", c);
      }
    }
    strcpy(name, pname);
  }

  else
  {
    errno = 0;
    while(fgets(arg, 256, stdin) != NULL){
      if( (value = strchr(arg, '\n')) == NULL)
        fail("line too long: '%s'\n", arg);
      *value = 0;
      if( (value = strchr(arg, '=')) == NULL)
        fail("invalid input: '%s'\n", arg);
      *value = 0;
      value++;
      if (!strcmp(arg, "agent")){
        strcpy(name, value);
        pname = name;
      }
      if (!strcmp(arg, "option")){
        if (!strcmp(value, "multipath"))
          multipath = 1;
        else
          printe("warning: option %s not recognized\n", value);
      }
      if (!strcmp(arg, "ipaddr") || !strcmp(arg, "nodename")){
        if (!strcmp(arg, "ipaddr"))
          printe("warning: 'ipaddr' key is depricated, please see man page\n");
        strcpy(ipaddr, value);
      }
      if (!strcmp(arg, "wait_time")){
        int match = sscanf(value, "%u", &wait_time);
        if (match != 1 || !wait_time)
          fail("'%s' is not a valid value for wait_time\n", value);
      }
      if (!strcmp(arg, "retrys")){
        int match = sscanf(value, "%u", &retrys);
        if (match != 1)
          fail("'%s' is not a valid value for retrys\n", value);
      }
      if (!strcmp(arg, "server") || !strcmp(arg, "servers"))
        parse_servers(value);
      errno = 0;
    }
    if (errno != 0)
      fail("couldn't read line: %s\n", strerror(errno));
  }
}


static void clear_serv(void){
  serv_list_t *prev = NULL;
  while(servers){
    prev = servers;
    servers = servers->next;
    free(prev);
  }
}

int do_fence(char *serv_name, int fence_server){

  int error, val;
  char line[256];
  int fds[2];
  int pid;

  if (pipe(fds))
    fail("pipe error\n");
      
  pid = fork();
  if (pid < 0)
    fail("can't fork fencing method\n");

  if (!pid){
    close(1);
    dup(fds[1]);
    close(fds[0]);
    close(fds[1]);
    close(2);
    dup(1);
    if (fence_server)
      error = execlp(NODE_FENCE_CMD, NODE_FENCE_CMD, serv_name, NULL);
    else
      error = execlp(GNBD_FENCE_CMD, GNBD_FENCE_CMD, "-s", ipaddr,
                     "-t", serv_name, NULL);
    printe("could not exec %s\n", (fence_server)? NODE_FENCE_CMD : GNBD_FENCE_CMD);
    exit(1);
  }
  else{
    close(0);
    dup(fds[0]);
    close(fds[0]);
    close(fds[1]);

    val = fcntl(0, F_GETFL, 0);
    if (val >= 0){
      val |= O_NONBLOCK;
      fcntl(0, F_SETFL, val);
    }
    waitpid(pid, &error, 0);
    if (WIFEXITED(error)){
      int count;
      error = WEXITSTATUS(error);
      while( (count = read(0, line, 255)) > 0){
        line[count] = 0;
        printf("%s", line);
      }
      return error;
    }
    else
      fail("%s exitted abnormally\n", (fence_server)? NODE_FENCE_CMD : GNBD_FENCE_CMD);
  }
}

void run_fence(char *serv_name){
  int trys;
  int err;
  if (multipath){
    for (trys = 0; trys <= retrys; trys++){
      err = do_fence(serv_name, 0);
      if (!err)
        return;
      if (err != SERVER_ERR)
        fail("%s, %s\n", pname, ipaddr);
      printe("try %d for server %s failed\n", trys + 1, serv_name);
      sleep(wait_time);
    }
    printe("fencing unresponsive server\n");
    err = do_fence(serv_name, 1);
    if (err)
      fail("could not fence unresponsive server %s\n", serv_name);
    return;
  }
  else{
    if (do_fence(serv_name, 0))
      fail("%s, %s\n", pname, ipaddr);
  }
}


int main(int argc, char **argv)
{
  serv_list_t *tmp;
  if (atexit(clear_serv) != 0)
    fail("can't register exit function\n");
  
  memset(arg, 0, 256);
  memset(name, 0, 256);
  memset(ipaddr, 0, 256);

  get_options(argc, argv);

  if (ipaddr[0] == '\0')
    fail("no IP addr\n");

  if (!servers)
    fail("missing server list\n");
  
  tmp = servers;
  while (tmp){
    run_fence(tmp->name);
    tmp = tmp->next;
  }

  if(!quiet_flag)
    printf("success: %s, %s\n", pname, ipaddr);
  
  return 0;
}
