#if 0
echo "Compiling cluster_cmd.";
gcc -lmagma -lmagmamsg -ldl cluster_cmd.c -o cluster_cmd
if [ $? != 0 ]; then
echo "Failed to compile cluster_cmd.";
echo "Perhaps you don't have the magma libraries installed?";
exit 1;
else
echo "Complete."
exit 0;
fi
#endif
/************************************************************
 ** This program executes a command on all cluster nodes
 ** that are currently "active" in the cluster.  It executes
 ** the command at a specific instance in time (t + timeofday).
 ** You can set that time or use the default (3 sec).  If
 ** you have ntp running on your cluster nodes, the command
 ** will execute at almost exactly the same time on each node.
 ** If you don't have ntp running, you may not get a response
 ** back from some of your nodes, because the command will
 ** fail to execute in time.
 **
 ** Proximity in execution times should be within ~1ms if
 ** used properly.
 ***********************************************************/
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <sys/time.h>
#include <string.h>
#include "magma.h"
#include "magmamsg.h"

#define CLIENT_BASE_PORT 25001
#define SERVER_BASE_PORT 25011

void print_usage(FILE *stream);
static char *capture_output(int fd);
static char *run_cmd(char *cmd, char *args[], time_t t);
static int await_command(void);
static int send_command(char *cmd, int t);

char *nodes[256];

int main(int argc, char *argv[]){
  int i,index=0;
  int t = 3;
  int pid;
  char cmd[256];

  memset(nodes, 0, sizeof(nodes));

  if(argc < 2){
    fprintf(stderr, "Wrong number of arguments.\n");
    exit(EXIT_FAILURE);
  }

  for(i=1; i < argc; i++){
    if(!strcmp(argv[i], "-d") ||
       !strcmp(argv[i], "--daemon")){
      printf("WARNING:: Use of this program opens a big security hole.\n");
      pid = fork();
      if(pid){
	exit(EXIT_SUCCESS);
      } else {
	await_command();
      }
      exit(EXIT_FAILURE);
    }
    if(!strcmp(argv[i], "-h") ||
       !strcmp(argv[i], "--help")){
      print_usage(stdout);
      exit(EXIT_SUCCESS);
    }
    if(!strcmp(argv[i], "-n") ||
       !strcmp(argv[i], "--node")){
      if(argc <= i+1){
	fprintf(stderr, "-t/--time requires an argument.\n");
	exit(EXIT_FAILURE);
      }
      i++;
      nodes[index++] = argv[i];
      continue;
    }
    if(!strcmp(argv[i], "-t") ||
       !strcmp(argv[i], "--time")){
      if(argc <= i+1){
	fprintf(stderr, "-t/--time requires an argument.\n");
	exit(EXIT_FAILURE);
      }
      i++;
      t = atoi(argv[i]);
      continue;
    }
    if(argv[i][0] == '-'){
      fprintf(stderr, "Unknown option '%s'.\n", argv[i]);
      exit(EXIT_FAILURE);
    }
    break;
  }

  memset(cmd, 0, sizeof(cmd));
  for(index=0; i < argc; i++){
    strncpy(cmd+index, argv[i], (sizeof(cmd)-index)-1);
    index = strlen(cmd);
    cmd[index++] = ' ';
  }

  if(!strlen(cmd)){
    fprintf(stderr, "No command specified.\n");
    exit(EXIT_FAILURE);
  }

  if(send_command(cmd, t)){
    exit(EXIT_FAILURE);
  }

  exit(EXIT_SUCCESS);
}

void print_usage(FILE *stream){
  fprintf(stream,
	  "cluster_cmd [options] <command>\n"
	  "\n"
	  "Options:\n"
	  "  -d/--daemon      Start the daemon.\n"
	  "  -h/--help        Print usage.\n"
	  "  -t/--time <sec>  Specify the number of seconds before command execution.\n"
	  );
}

static char *capture_output(int fd)
{
  char *rtn = NULL;
  char buf[512];
  
  memset(buf, 0, sizeof(buf));
  
  while (read(fd, buf, sizeof(buf)-1) > 0) {
    if(rtn){
      rtn = realloc(rtn, (strlen(buf)+1)+strlen(rtn));
      strncpy(rtn+strlen(rtn), buf, strlen(buf));
    } else {
      rtn = malloc(strlen(buf)+1);
      memset(rtn, 0, strlen(buf)+1);
      strncpy(rtn, buf, strlen(buf));
    }
    memset(buf, 0, sizeof(buf));
  }
  return rtn;
}


static char *run_cmd(char *cmd, char *args[], time_t t)
{
  int pid, status;
  int pr_fd, pw_fd;  /* parent read/write file descriptors */
  int cr_fd, cw_fd;  /* child read/write file descriptors */
  int fd1[2];
  int fd2[2];
  char *rtn;
  struct timeval exec_time;
                                                                                
  cr_fd = cw_fd = pr_fd = pw_fd = -1;
  
  if (pipe(fd1))
    goto fail;
  pr_fd = fd1[0];
  cw_fd = fd1[1];
  
  if (pipe(fd2))
    goto fail;
  cr_fd = fd2[0];
  pw_fd = fd2[1];
  
  pid = fork();
  if (pid < 0)
    goto fail;
  
  if (pid) {
    /* parent */
    fcntl(pr_fd, F_SETFL, fcntl(pr_fd, F_GETFL, 0) | O_NONBLOCK);
    
    close(pw_fd);
    waitpid(pid, &status, 0);
    
    if (!WIFEXITED(status)){
      goto fail;
    } else {
      rtn = capture_output(pr_fd);
    } 
  } else {
    /* child */
    
    close(0);
    dup(cr_fd);
    close(1);
    dup(cw_fd);
    close(2);
    dup(cw_fd);
    /* keep cw_fd open so parent can report all errors. */
    close(pr_fd);
    close(pw_fd);
    gettimeofday(&exec_time, NULL);
    exec_time.tv_sec = (t - exec_time.tv_sec)-1;
    exec_time.tv_usec= 1000000 - exec_time.tv_usec;
    if(exec_time.tv_sec & (1<<(sizeof(time_t)*8-1))){
      /* Overflow detected -- Executing now! */
    } else {
      select(0, NULL, NULL, NULL, &exec_time);
    }
    execvp(cmd, args);
    exit(EXIT_FAILURE);
  }
  
  close(pr_fd);
  close(cw_fd);
  close(cr_fd);
  close(pw_fd);
  return rtn;
  
 fail:
  close(pr_fd);
  close(cw_fd);
  close(cr_fd);
  close(pw_fd);
  return NULL;
}


static int await_command(void){
  int fds[2];
  int fd;
  int afd;
  uint64_t nodeid;
  int max_fds, n;
  int cluster_fd = -1;
  fd_set rset;
  char buffer[512];
  cluster_member_list_t *membership = NULL;
  
  if(msg_listen(CLIENT_BASE_PORT, 0, fds, 2) <= 0){
    return -1;
  }

 restart:
  while(cluster_fd < 0){
    cluster_fd = clu_connect(NULL, 0);
  }

  membership = clu_member_list(NULL);
  msg_update(membership);
  memb_resolve_list(membership, NULL);

  while(1){
    FD_ZERO(&rset);
    max_fds = msg_fill_fdset(&rset, MSG_ALL, MSGP_ALL);
    n = select(max_fds+1, &rset, NULL, NULL, NULL);
                                                                                
    if(n < 0){
      continue;
    }
                                                                                
    while(n){
      memset(buffer, 0, sizeof(buffer));
      fd = msg_next_fd(&rset);
      if(fd == -1) { break; }
      n--;
      if(fd == cluster_fd){
	switch(clu_get_event(cluster_fd)){
	case CE_NULL:
	case CE_MEMB_CHANGE:
	case CE_QUORATE:
	case CE_INQUORATE:
	  cml_free(membership);
          membership = clu_member_list(NULL);
          msg_update(membership);
          memb_resolve_list(membership, NULL);
	  continue;
	case CE_SHUTDOWN:
	default:
	  clu_disconnect(cluster_fd);
	  cluster_fd = -1;
	  goto restart;
	}
      }

      afd = msg_accept(fd, 1, &nodeid);
      if(afd < 0){
	continue;
      }
      msg_receive(afd, buffer, sizeof(buffer));
      msg_close(afd);

      if(!strncmp(buffer, "COMMAND", 7)){	/* execute command */
	char *tmp, *tmp2;
	char *args[64];
	int i=-1;
	time_t t;

	memset(args, 0, sizeof(args));

	for(tmp = strstr(buffer, " ")+1; tmp < buffer+strlen(buffer); tmp = tmp2){
	  tmp2 = strstr(tmp, " ");
	  if(!tmp2){
	    break;
	  }
	  if(i < 0){
	    i++;
	    args[0] = (char *)strndup(tmp, tmp2-tmp);
	    t = atoi(args[0]);
	    free(args[0]);
	    args[0] = NULL;
	  } else {

	    args[i++] = (char *)strndup(tmp, tmp2-tmp);
	  }
	  tmp2++;
	}
	if(args[0]){
	  tmp = run_cmd(args[0], args, t);
	  fd =  msg_open(nodeid, SERVER_BASE_PORT, 0, 5);
	  if(fd < 0){
	    continue;
	  }

	  if(tmp){
	    msg_send(fd, tmp, strlen(tmp));
	    free(tmp);
	  } else {
	    msg_send(fd, "\n", 1);
	  }
	  msg_close(fd);
	}
      }
    }
  }
  return -1;
}


static int send_command(char *cmd, int t){
  int i, j, fds[2], fd, afd, n, max_fds;
  int count;
  int cluster_fd = -1;
  int resp=0;
  uint64_t nodeid;
  uint64_t *ids;
  fd_set rset;
  char buffer[512];
  struct timeval timeout;
  struct timeval exec_time;
  cluster_member_list_t *membership = NULL;

  gettimeofday(&exec_time, NULL);

  memset(buffer, 0, sizeof(buffer));
  sprintf(buffer, "COMMAND %u ", exec_time.tv_sec+t);
  strncpy(buffer+strlen(buffer), cmd, (sizeof(buffer)-strlen(buffer))-1);

  cluster_fd = clu_connect(NULL, 0);
  if(cluster_fd < 0){
    fprintf(stderr, "Unable to connect to cluster infrastructure.\n");
    return -1;
  }

  if(msg_listen(SERVER_BASE_PORT, 0, fds, 2) <= 0){
    return -1;
  }

  membership = clu_member_list(NULL);
  msg_update(membership);
  memb_resolve_list(membership, NULL);

  if(!membership->cml_count){
    fprintf(stderr, "No members.\n");
    return -1;
  }

  if(!nodes[0]){
    count = membership->cml_count;
  } else {
    for(i=0, count=0; nodes[i]; i++){
      count++;
    }
  }
  ids = malloc(sizeof(uint64_t)*count);
  if(!ids){
    fprintf(stderr, "Not enough memory.\n");
    return -1;
  }
  memset(ids, 0, sizeof(uint64_t)*count);

  for(i = 0; i < count; i++){
    if(!nodes[0]){
      ids[i] = membership->cml_members[i].cm_id;
    } else {
      for(j=0; j < membership->cml_count; j++){
	if(!strcmp(nodes[i], membership->cml_members[j].cm_name)){
	  ids[i] = membership->cml_members[j].cm_id;
	  break;
	}
      }
      if(j == membership->cml_count){
	fprintf(stderr, "Unable to find node, %s, in membership.\n", nodes[i]);
	return -1;
      }
    }
  }

  for(i=0; i < count; i++){
    fd =  msg_open(ids[i], CLIENT_BASE_PORT, 0, 5);
    if(fd < 0){
      fprintf(stderr, "ERROR:: Unable to communicate with %s\n"
	      "  Is the cluster_cmd daemon running there?\n",
	      memb_id_to_name(membership, ids[i]));
      continue;
    }
    msg_send(fd, buffer, strlen(buffer));
    msg_close(fd);
  }

  while(1){
    FD_ZERO(&rset);
    max_fds = msg_fill_fdset(&rset, MSG_ALL, MSGP_ALL);
    /* have to wait at least as long a it takes before clients exec */
    timeout = (struct timeval){t+1,0};
    n = select(max_fds+1, &rset, NULL, NULL, &timeout);
                                                                                
    if(n < 0){
      continue;
    }
    
    if(!n){
      break;
    }
    
    while(n){
      memset(buffer, 0, sizeof(buffer));
      fd = msg_next_fd(&rset);
      if(fd == -1) { break; }
      n--;
      if(fd == cluster_fd){
	switch(clu_get_event(cluster_fd)){
	case CE_NULL:
	case CE_MEMB_CHANGE:
	case CE_QUORATE:
	case CE_INQUORATE:
	  cml_free(membership);
          membership = clu_member_list(NULL);
          msg_update(membership);
          memb_resolve_list(membership, NULL);
	  continue;
	case CE_SHUTDOWN:
	default:
	  clu_disconnect(cluster_fd);
	  cluster_fd = -1;
	  return -1;
	}
      }
	
      resp++;
      afd = msg_accept(fd, 1, &nodeid);
      msg_receive(afd, buffer, sizeof(buffer));
      msg_close(afd);
      printf("\nRESPONSE from %s::\n", memb_id_to_name(membership, nodeid));
      printf("%s\n", buffer);
    }
  }
  
  if(resp == 0){
    fprintf(stderr, "No responses.\n");
  } else {
    printf("%d responses.\n", resp);
  }
  return 0;
}

