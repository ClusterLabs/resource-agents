#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>

#include "ccs.h"

#include "copyright.cf"

static void print_usage(FILE *stream);

static int disconnect() {
	if (ccs_disconnect(1) < 0)
		return 1;

	return 0;
}

int main(int argc, char *argv[]){
  int desc=0;
  int i=0;
  int error = 0;
  int force = 0, blocking = 0;
  char *str=NULL;
  char *cluster_name = NULL;

  if(argc <= 1){
    print_usage(stderr);
    exit(EXIT_FAILURE);
  }

  for(i=1; i < argc; i++){
    if(!strcmp(argv[i], "-h")){
      print_usage(stdout);
      exit(EXIT_SUCCESS);
    }
    if(!strcmp(argv[i], "-V")){
      printf("%s %s (built %s %s)\n", argv[0], RELEASE_VERSION, __DATE__, __TIME__);
      printf("%s\n", REDHAT_COPYRIGHT);
      exit(EXIT_SUCCESS);
    }
  }

  if(!strcmp(argv[1], "connect")){
    for(i=2; i < argc; i++){
      if(!strcmp(argv[i], "force")){
	printf("Force is set.\n");
	force = 1;
      } else if(!strcmp(argv[i], "block")){
	printf("Blocking is set.\n");
	blocking = 1;
      } else {
	cluster_name = argv[i];
	printf("Setting cluster name to %s\n", cluster_name);
      }
    }
    if(blocking && !force){
      fprintf(stderr, "Blocking can only be used with \"force\".\n");
      exit(EXIT_FAILURE);
    }
    if(force){
      desc = ccs_force_connect(cluster_name, blocking);
    } else {
      if(cluster_name){
	fprintf(stderr, "A cluster name can only be specified when using 'force'.\n");
	exit(EXIT_FAILURE);
      }
      desc = ccs_connect();
    }
    if(desc < 0){
      fprintf(stderr, "ccs_connect failed: %s\n", strerror(-desc));
      exit(EXIT_FAILURE);
    } else {
      printf("Connect successful.\n");
      printf(" Connection descriptor = %d\n", desc);
      disconnect();
    }
  }
  else if(!strcmp(argv[1], "disconnect")){
    if(argc < 3){
      fprintf(stderr, "Wrong number of arguments.\n");
      exit(EXIT_FAILURE);
    }
    desc = ccs_connect();
    if((error = disconnect())){
      fprintf(stderr, "ccs_disconnect failed: %s\n", strerror(-error));
      exit(EXIT_FAILURE);
    } else {
      printf("Disconnect successful.\n");
    }
  }
  else if(!strcmp(argv[1], "get")){
    if(argc < 4){
      fprintf(stderr, "Wrong number of arguments.\n");
      exit(EXIT_FAILURE);
    }
    desc = ccs_connect();
    if((desc < 0) || (error = ccs_get(desc, argv[3], &str))){
      fprintf(stderr, "ccs_get failed: %s\n", strerror(-error));
      exit(EXIT_FAILURE);
    } else {
      printf("Get successful.\n");
      printf(" Value = <%s>\n", str);
      if(str)free(str);
      disconnect();
    }
  }
  else if(!strcmp(argv[1], "set")){
    if(argc < 5){
      fprintf(stderr, "Wrong number of arguments.\n");
      exit(EXIT_FAILURE);
    }
    desc = ccs_connect();
    if((desc < 0) || (error = ccs_set(desc, argv[3], argv[4]))){
      fprintf(stderr, "ccs_set failed: %s\n", strerror(-error));
      exit(EXIT_FAILURE);
    } else {
      printf("Set successful.\n");
      disconnect();
    }
  }
  else {
    fprintf(stderr, "Unknown command: %s\n", argv[1]);
    exit(EXIT_FAILURE);
  }

  exit(EXIT_SUCCESS);
}


static void print_usage(FILE *stream){
  fprintf(stream,
	  "Usage:\n"
	  "\n"
	  "ccs_test [Options] <Command>\n"
	  "\n"
	  "Options:\n"
	  "  -h                        Print usage.\n"
	  "  -V                        Print version information.\n"
	  "\n"
	  "Commands:\n"
	  "  connect <force> <block>   Connect to CCS and return connection descriptor.\n"
	  "  disconnect <desc>         Disconnect from CCS.\n"
	  "  get <desc> <request>      Get a value from CCS.\n"
	  "  set <desc> <path> <val>   Set a value in CCS.\n"
	  );
}
