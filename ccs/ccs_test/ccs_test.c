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
#include <errno.h>
#include <string.h>

#include "ccs.h"

#include "copyright.cf"

static void print_usage(FILE *stream);

int main(int argc, char *argv[]){
  int desc=0;
  int i=0;
  int error = 0;
  int force = 0, blocking = 0;
  char *str=NULL, *str2=NULL;
  char *cluster_name = NULL;

  if(argc <= 1){
    print_usage(stderr);
    exit(EXIT_FAILURE);
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
    }
  }
  else if(!strcmp(argv[1], "disconnect")){
    if(argc < 3){
      fprintf(stderr, "Wrong number of arguments.\n");
      exit(EXIT_FAILURE);
    }
    desc = atoi(argv[2]);
    if((error = ccs_disconnect(desc))){
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
    desc = atoi(argv[2]);
    if((error = ccs_get(desc, argv[3], &str))){
      fprintf(stderr, "ccs_get failed: %s\n", strerror(-error));
      exit(EXIT_FAILURE);
    } else {
      printf("Get successful.\n");
      printf(" Value = <%s>\n", str);
      if(str)free(str);
    }
  }
  else if(!strcmp(argv[1], "set")){
    if(argc < 5){
      fprintf(stderr, "Wrong number of arguments.\n");
      exit(EXIT_FAILURE);
    }
    desc = atoi(argv[2]);
    if((error = ccs_set(desc, argv[3], argv[4]))){
      fprintf(stderr, "ccs_set failed: %s\n", strerror(-error));
      exit(EXIT_FAILURE);
    } else {
      printf("Set successful.\n");
    }
  }
  else if(!strcmp(argv[1], "get_state")){
    if(argc < 3){
      fprintf(stderr, "Wrong number of arguments.\n");
      exit(EXIT_FAILURE);
    }
    desc = atoi(argv[2]);
    if((error = ccs_get_state(desc, &str, &str2))){
      fprintf(stderr, "ccs_get_state failed: %s\n", strerror(-error));
      exit(EXIT_FAILURE);
    } else {
      printf("Get state successful.\n");
      printf(" Current working path: %s\n", str);
      printf(" Previous query      : %s\n", str2);
      if(str) free(str);
      if(str2) free(str2);
    }
  }
  else if(!strcmp(argv[1], "set_state")){
    if(argc < 4){
      fprintf(stderr, "Wrong number of arguments.\n");
      exit(EXIT_FAILURE);
    }
    desc = atoi(argv[2]);
    if((error = ccs_set_state(desc, argv[3], 0))){
      fprintf(stderr, "ccs_set_state failed: %s\n", strerror(-error));
      exit(EXIT_FAILURE);
    } else {
      printf("Set state successful.\n");
    }
  } else {
    fprintf(stderr, "Unknown command: %s\n", argv[1]);
    exit(EXIT_FAILURE);
  }

  exit(EXIT_SUCCESS);
}


static void print_usage(FILE *stream){
  fprintf(stream,
	  "Usage:\n"
	  "\n"
	  "ccs_test <Command>\n"
	  "\n"
	  "Commands:\n"
	  "  connect <force> <block>  Connect to CCS and return connection descriptor.\n"
	  "  disconnect <desc>        Disconnect from CCS.\n"
	  "  get <desc> <request>     Get a value from CCS.\n"
	  "  set <desc> <path> <val>  Set a value in CCS.\n"
	  "  get_state <desc>         Get the state in the connection.\n"
	  "  set_state <desc> <ncwp>  Set the current working path.\n"
	  );
}
