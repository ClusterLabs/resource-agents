#include <stdio.h>
#include <stdlib.h>
#include <getopt.h>
#include <string.h>

#include "copyright.cf"
#include "update.h"
#include "upgrade.h"
#include "editconf.h"

static void print_usage(FILE *stream);

int main(int argc, char *argv[])
{
  optind = 1;

  if (argc < 2 || !strcmp(argv[optind], "-h")) {
      print_usage(stdout);
      exit(EXIT_SUCCESS);
  }
  if (!strcmp(argv[optind], "-V")) {
      printf("%s %s (built %s %s)\n", argv[0], CCS_RELEASE_NAME,
	     __DATE__, __TIME__);
      printf("%s\n", REDHAT_COPYRIGHT);
      exit(EXIT_SUCCESS);
  }

  if(optind < argc){
    if(!strcmp(argv[optind], "help")){
      print_usage(stdout);
      exit(EXIT_SUCCESS);
    }
    else if(!strcmp(argv[optind], "update")){
      if(optind+1 >= argc){
	fprintf(stderr, "Too few arguments.\n"
		"Try 'ccs_tool help' for help.\n");
	exit(EXIT_FAILURE);
      }
      if(update(argv[optind+1])){
	fprintf(stderr, "\nFailed to update config file.\n");
	exit(EXIT_FAILURE);
      }
      printf("\nUpdate complete.\n");
    }
    else if(!strcmp(argv[optind], "upgrade")){
      if(optind+1 >= argc){
	fprintf(stderr, "Too few arguments.\n"
		"Try 'ccs_tool help' for help.\n");
	exit(EXIT_FAILURE);
      }
      if(upgrade(argv[optind+1])){
	fprintf(stderr, "\nFailed to upgrade CCS configuration information.\n");
	exit(EXIT_FAILURE);
      }
    }

    else if(!strcmp(argv[optind], "addnode")){
	    add_node(argc-1, argv+1);
	    exit(EXIT_FAILURE);
    }
    else if(!strcmp(argv[optind], "delnode")){
	    del_node(argc-1, argv+1);
	    exit(EXIT_FAILURE);
    }
    else if(!strcmp(argv[optind], "addfence")){
	    add_fence(argc-1, argv+1);
	    exit(EXIT_FAILURE);
    }
    else if(!strcmp(argv[optind], "delfence")){
	    del_fence(argc-1, argv+1);
	    exit(EXIT_FAILURE);
    }
    else if(!strcmp(argv[optind], "lsnode")){
	    list_nodes(argc-1, argv+1);
	    exit(EXIT_FAILURE);
    }
    else if(!strcmp(argv[optind], "lsfence")){
	    list_fences(argc-1, argv+1);
	    exit(EXIT_FAILURE);
    }
    else if(!strcmp(argv[optind], "create")){
	    create_skeleton(argc-1, argv+1);
	    exit(EXIT_FAILURE);
    }
    else if(!strcmp(argv[optind], "addnodeids")){
	    add_nodeids(argc-1, argv+1);
	    exit(EXIT_FAILURE);
    }

    else {
      fprintf(stderr, "Unknown command, %s.\n"
	      "Try 'ccs_tool help' for help.\n", argv[optind]);
      exit(EXIT_FAILURE);
    }
  } else {
    fprintf(stderr, "Too few arguments.\n"
	    "Try 'ccs_tool help' for help.\n");
    exit(EXIT_FAILURE);
  }
  exit(EXIT_SUCCESS);
}

static void print_usage(FILE *stream){
  fprintf(stream,
	  "Usage::\n"
	  "  ccs_tool [options] <command>\n"
	  "\n"
	  "Options:\n"
	  "  -h                  Print this usage and exit.\n"
	  "  -V                  Print version information and exit.\n"
	  "\n"
	  "Commands:\n"
	  "  help                Print this usage and exit.\n"
	  "  update <xml file>   Tells ccsd to upgrade to new config file.\n"
	  "  upgrade <location>  Upgrade old CCS format to new xml format.\n"
	  "  addnode <node>      Add a node\n"
          "  delnode <node>      Delete a node\n"
          "  lsnode              List nodes\n"
          "  lsfence             List fence devices\n"
	  "  addfence <fencedev> Add a new fence device\n"
	  "  delfence <fencedev> Delete a fence device\n"
	  "  create              Create a skeleton config file\n"
	  "  addnodeids          Assign node ID numbers to all nodes\n"
	  "\n");
}
