#include <stdio.h>
#include <stdlib.h>
#include <getopt.h>
#include <string.h>
#include <errno.h>

#include "copyright.cf"
#include "update.h"
#include "editconf.h"
#ifdef LEGACY_CODE
#include "libccscompat.h"
#else
#include "ccs.h"
#endif


/*
 * Old libccs retruned -error (mostly!) but didn't set errno (sigh)
 * New libccs sets errno correctly
 */
static char *errstring(int retcode)
{
#ifdef LEGACY_CODE
	return strerror(retcode);
#else
	return strerror(errno);
#endif
}

static void tool_print_usage(FILE *stream);

int globalverbose=0;

static void test_print_usage(FILE *stream);

static int test_main(int argc, char *argv[], int old_format){
  int desc=0;
  int i=0;
  int error = 0;
  int force = 0, blocking = 0;
  char *str=NULL;
  char *cluster_name = NULL;

  if(argc <= 1){
    test_print_usage(stderr);
    exit(EXIT_FAILURE);
  }

  for(i=1; i < argc; i++){
    if(!strcmp(argv[i], "-h")){
      test_print_usage(stdout);
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
      fprintf(stderr, "ccs_connect failed: %s\n", errstring(-desc));
      exit(EXIT_FAILURE);
    } else {
      printf("Connect successful.\n");
      printf(" Connection descriptor = %d\n", desc);
      ccs_disconnect(desc);
    }
  }
  else if(!strcmp(argv[1], "ccs_disconnect")){
    if(argc < 3){
      fprintf(stderr, "Wrong number of arguments.\n");
      exit(EXIT_FAILURE);
    }
    desc = ccs_connect();
    if((error = ccs_disconnect(desc))){
      fprintf(stderr, "ccs_ccs_disconnect failed: %s\n", errstring(-error));
      exit(EXIT_FAILURE);
    } else {
      printf("Ccs_Disconnect successful.\n");
    }
  }
  else if(!strcmp(argv[1], "get")){
    if(argc < 4){
      fprintf(stderr, "Wrong number of arguments.\n");
      exit(EXIT_FAILURE);
    }
    desc = ccs_connect();
    if((desc < 0) || (error = ccs_get(desc, argv[3], &str))){
      fprintf(stderr, "ccs_get failed: %s\n", errstring(-error));
      exit(EXIT_FAILURE);
    } else {
	    if (old_format) {
		    printf("Get successful.\n");
		    printf(" Value = <%s>\n", str);
	    }
	    else {
		    printf("%s\n", str);
	    }
      if(str)free(str);
      ccs_disconnect(desc);
    }
  }
  else {
    fprintf(stderr, "Unknown command: %s\n", argv[1]);
    exit(EXIT_FAILURE);
  }

  exit(EXIT_SUCCESS);
}

static void test_print_usage(FILE *stream)
{
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
	  "  ccs_disconnect <desc>     Disconnect from CCS.\n"
	  "  get <desc> <request>      Get a value from CCS.\n"
	  );
}

#ifndef LEGACY_CODE
static int xpath_query(int argc, char **argv)
{
	int handle;
	char *ret;
	int i;

	if (argc < 2) {
		fprintf(stderr,
			"Usage:\n"
			"\n"
			"ccs_tool query <xpath query>\n");
		return 1;
	}

	/* Tell the library we want full XPath parsing */
	fullxpath = 1;

	handle = ccs_connect();

	/* Process all the queries on the command-line */
	for (i=1; i<argc; i++) {
		if (!ccs_get(handle, argv[1], &ret)) {
			printf("%s\n", ret);
			free(ret);
		}
		else {
			fprintf(stderr, "Query failed: %s\n", strerror(errno));
			ccs_disconnect(handle);
			return -1;
		}
	}
	ccs_disconnect(handle);
	return 0;
}
#endif

static int tool_main(int argc, char *argv[])
{
  optind = 1;

  if (argc < 2 || !strcmp(argv[optind], "-h")) {
      tool_print_usage(stdout);
      exit(EXIT_SUCCESS);
  }
  if (!strcmp(argv[optind], "-V")) {
      printf("%s %s (built %s %s)\n", argv[0], RELEASE_VERSION,
	     __DATE__, __TIME__);
      printf("%s\n", REDHAT_COPYRIGHT);
      exit(EXIT_SUCCESS);
  }

  if(optind < argc){
    if(!strcmp(argv[optind], "-verbose")){
      optind++;
      globalverbose=1;
    }
    if(!strcmp(argv[optind], "help")){
      tool_print_usage(stdout);
      exit(EXIT_SUCCESS);
    }
#ifdef LEGACY_CODE
    /* Update is meaningless now */
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
    /* Do old ccs queries */
    else if(!strcmp(argv[optind], "query")){
	    char *new_argv[argc+2];
	    int i;

	    new_argv[0] = "ccs_test";
	    new_argv[1] = "get";
	    new_argv[2] = "0"; /* Dummy connection ID */
	    for (i=2; i<argc; i++)
		    new_argv[1+i] = argv[i];

	    return test_main(argc+1, new_argv, 0);
    }
#else
    else if(!strcmp(argv[optind], "query")){
	    return xpath_query(argc-1, argv+1);
    }
#endif
    else if(!strcmp(argv[optind], "addnode")){
	    add_node(argc-1, argv+1);
	    exit(EXIT_SUCCESS);
    }
    else if(!strcmp(argv[optind], "delnode")){
	    del_node(argc-1, argv+1);
	    exit(EXIT_SUCCESS);
    }
    else if(!strcmp(argv[optind], "addfence")){
	    add_fence(argc-1, argv+1);
	    exit(EXIT_SUCCESS);
    }
    else if(!strcmp(argv[optind], "delfence")){
	    del_fence(argc-1, argv+1);
	    exit(EXIT_SUCCESS);
    }
    else if(!strcmp(argv[optind], "lsnode")){
	    list_nodes(argc-1, argv+1);
	    exit(EXIT_SUCCESS);
    }
    else if(!strcmp(argv[optind], "lsfence")){
	    list_fences(argc-1, argv+1);
	    exit(EXIT_SUCCESS);
    }
    else if(!strcmp(argv[optind], "create")){
	    create_skeleton(argc-1, argv+1);
	    exit(EXIT_SUCCESS);
    }
    else if(!strcmp(argv[optind], "addnodeids")){
	    add_nodeids(argc-1, argv+1);
	    exit(EXIT_SUCCESS);
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

static void tool_print_usage(FILE *stream){
  fprintf(stream,
	  "Usage:\n"
	  "  ccs_tool [options] <command>\n"
	  "\n"
	  "Options:\n"
	  "  -verbose            Make some operations print more details.\n"
	  "  -h                  Print this usage and exit.\n"
	  "  -V                  Print version information and exit.\n"
	  "\n"
	  "Commands:\n"
	  "  help                Print this usage and exit.\n"
#ifdef LEGACY_CODE
	  "  update <xml file>   Tells ccsd to upgrade to new config file version.\n"
	  "  query <ccs query>   Query the cluster configuration.\n"
#else
	  "  query <xpath query> Query the cluster configuration.\n"
#endif
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


int main(int argc, char *argv[])
{
	char *name = strdup(argv[0]);

	/*
	 * Don't be anal about the binary name.
	 * We expect either 'ccs_tool' or 'ccs_test',
	 * but interpret anything other than 'ccs_test'
	 * as 'ccs_tool'.
	 * That's not a bug, it's a feature.
	 */

	if (strcmp(basename(name), "ccs_test") == 0)
		return test_main(argc, argv, 1);
	else
		return tool_main(argc, argv);
}
