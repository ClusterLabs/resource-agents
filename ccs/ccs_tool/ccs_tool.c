#include <stdio.h>
#include <stdlib.h>
#include <getopt.h>
#include <string.h>

#include "copyright.cf"
#include "update.h"
#include "upgrade.h"

static void print_usage(FILE *stream);

int main(int argc, char *argv[]){
  int c;

  while((c = getopt(argc, argv, "hV")) != -1){
    switch(c){
    case 'h':
      print_usage(stdout);
      exit(EXIT_SUCCESS);
      break;
    case 'V':
      printf("%s %s (built %s %s)\n", argv[0], CCS_RELEASE_NAME,
	     __DATE__, __TIME__);
      printf("%s\n", REDHAT_COPYRIGHT);
      exit(EXIT_SUCCESS);
      break;      
    default:
      print_usage(stderr);
      exit(EXIT_FAILURE);
      break;
    }
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
	  "\n"
	  );
}
