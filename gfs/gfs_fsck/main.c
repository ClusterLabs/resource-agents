/******************************************************************************
*******************************************************************************
**
**  Copyright (C) Sistina Software, Inc.  1997-2003  All rights reserved.
**  Copyright (C) 2004 Red Hat, Inc.  All rights reserved.
**
**  This copyrighted material is made available to anyone wishing to use,
**  modify, copy, or redistribute it subject to the terms and conditions
**  of the GNU General Public License v.2.
**
*******************************************************************************
******************************************************************************/

/* For a description of gfs_fsck passes, look in gfsck_outline.txt */
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include <ctype.h>
#include <signal.h>
#include <time.h>

#include "allocation.h"
#include "bitmap.h"
#include "initialize.h"
#include "interactive.h"
#include "fs_incore.h"

#include "pass1.h"
#include "pass2.h"
#include "pass3.h"
#include "pass4.h"
#include "pass5.h"
#include "pass6.h"
#include "pass7.h"

#include "copyright.cf"

#ifdef DEBUG
static int sleep_between_passes = 2;
#else
static int sleep_between_passes = 0;
#endif

static char *parse_args(int argc, char *argv[]);
static void sig_int();
static void rerun_response(void);
static char *print_time(char *, time_t);
static int (*pass[])() = { pass_1, pass_2, pass_3, pass_4,
			   pass_5, pass_6, pass_7, NULL };


/**
 * main
 * @argc:
 * @argv:
 *
 */
int main(int argc, char **argv)
{
  fs_sbd_t *sdp;
  int i, error=0;
  char *device;
  time_t pt1=0,pt2=0,t1=0,t2=0;
  char time_str[100];

  /* Exits on failure */
  device = parse_args(argc, argv);

  if(!device){
    fprintf(stderr, "%s:  No device specified.  (Try -h for help.)\n",
	    argv[0]);
    exit(EXIT_FAILURE);
  }
  if(initialize(&sdp, device)){
    pp_print(PPVH, "Failed to initialize critical file system structures.\n");
    error = -1;
    goto fail;
  }

  if(signal(SIGINT, sig_int) == SIG_ERR){
    pp_print(PPVH, "Unable to set capture for SIGINT signal.\n");
    if(!query("Would you like to proceed? (y/n) ")){
      exit(EXIT_FAILURE);
    }
  }

  t1 = time(NULL);

 rerun:

  rerun_response();
  free_bitmaps(sdp);
  allocate_bitmaps(sdp);

  for(i=0; pass[i]; i++){
    if(sleep_between_passes){
      printf("Sleeping %d sec...\n", sleep_between_passes);
      sleep(15);
    }
    pt1 = time(NULL);
    error = pass[i](sdp);
    pt2 = time(NULL);
    switch(error){
    case -1:
      goto fail;
    case 0:
      pp_print(PPH,"Pass %d:  done  (%s)\n", i+1,
	       print_time(time_str, pt2-pt1));
      break;
    default:
      goto rerun;
    }
  }

 fail:
  cleanup(&sdp);
  sync();

  if(!error){
    t2 = time(NULL);
    pp_print(PPVH, "gfs_fsck Complete  (%s).\n", print_time(time_str, t2-t1));
    exit(EXIT_SUCCESS);
  } else {
    die("Failed to complete gfs_fsck.\n");
    exit(EXIT_FAILURE);
  }
}


/**
 * print_usage
 * @stream: stream to print to (e.g. stdio/stderr)
 *
 */
static void print_usage(FILE *stream)
{
  fprintf(stream, "Usage:\n\n"
	  "gfs_fsck [options] <BlockDevice>\n\n"
	  "Options:\n"
	  "  -h               Print this help, then exit.\n"
	  "  -q               Quiet.\n"
	  "  -V               Print program version information.\n"
	  "  -v               Verbose.\n"
	  "  -y               Assume \"yes\" to all questions.\n"
	  );
}	


/* parse_args
 * @argc:
 * @argv
 *
 * This function does not fail.  It will exit the program if there
 * is a problem.
 *
 * Returns: device name from argument list
 */
static char *parse_args(int argc, char *argv[]){
  int c;

  while ((c = getopt(argc, argv, ":hqVvy")) != -1){
    switch (c){

    case 'h':
      print_usage(stdout);
      exit(EXIT_SUCCESS);
      break;

    case 'q':
      pp_level=PPVH+1;
      break;

    case 'V':
      printf("gfs_fsck %s (built %s %s)\n", 
	     GFS_RELEASE_NAME, __DATE__, __TIME__);
      printf("%s\n", REDHAT_COPYRIGHT);
      exit(EXIT_SUCCESS);
      break;
      
    case 'v':
      pp_level--;
      if(pp_level < PPVL)
	pp_level = PPVL;
      break;

    case 'y':
      interactive=0;
      break;

    case ':':
      fprintf(stderr, "%s: option requires an argument -- %c.\n",
	      argv[0], optopt);
      fprintf(stderr, "Please see '-h' for usage.\n");
      exit(EXIT_FAILURE);
      break;
         
    case '?': /* Handle -v w/ or w/o arg */
      fprintf(stderr, "%s: invalid option -- %c\n", argv[0], optopt);
      fprintf(stderr, "Please use '-h' for usage.\n");
      exit(EXIT_FAILURE);
      break;
    
    default:
      fprintf(stderr, "Bad programmer! You forgot to catch the %c flag\n", c);
      exit(EXIT_FAILURE);
      break;
    }
  }

  return argv[optind];
}


/*
 * sig_int - capture SIGINT signal
 *
 * This function will tell the user that it's not a good
 * idea to kill a gfs_fsck, and give options for continuing
 * or quitting.
 */
static void sig_int(){
  int selection;
  char input_str[256];
#ifdef DEBUG
  uint64 *location;
#endif

  fprintf(stderr,
	  "\n\n"
	  "Warning :: gfs_fsck has not completed.  Quitting now\n"
	  "           could leave the file system corrupted.\n");
  while(1){
    fprintf(stderr,
	    "\n"
	    "Options ::\n"
	    "  1) Increase verbosity.\n"
	    "  2) Decrease verbosity.\n"
	    "  3) Toggle interactive mode.\n"
	    "  4) Continue.\n"
	    "  5) Exit.\n"
#ifdef DEBUG
	    "\n"
	    "  6) Print memory location.\n"
	    "  7) Print current memory usage.\n"
	    "  8) Assign sleep time between passes.\n"
#endif
	    "\n> ");

    fgets(input_str, sizeof(input_str), stdin);
    if(!isdigit((int)input_str[0])){
      fprintf(stderr, "Invalid option.\n");
      continue;
    }
    sscanf(input_str, " %d", &selection);
    switch(selection){
    case 1:
      pp_level--;
      printf("Verbose level:: [ %s ]\n",
	     (pp_level < PPVL)? "DEBUG":
	     (pp_level == PPVL)? "* * * * 5":
	     (pp_level == PPL)? "* * * 4 *":
	     (pp_level == PPN)? "* * 3 * *":
	     (pp_level == PPH)? "* 2 * * *":
	     (pp_level == PPVH)? "1 * * * *":
	     "QUIET");
      break;
    case 2:
      pp_level++;
      /* verbosity is the inverse of priority... */
      printf("Verbose level:: [ %s ]\n",
	     (pp_level < PPVL)? "DEBUG":
	     (pp_level == PPVL)? "* * * * 5":
	     (pp_level == PPL)? "* * * 4 *":
	     (pp_level == PPN)? "* * 3 * *":
	     (pp_level == PPH)? "* 2 * * *":
	     (pp_level == PPVH)? "1 * * * *":
	     "QUIET");
      break;
    case 3:
      interactive = (interactive)? 0: 1;
      printf("Interactive mode:: %s\n",
	     (interactive)? "ON": "OFF");
      break;
    case 4:
      printf("Continuing gfs_fsck...\n");
      goto out;
      break;
    case 5:
      printf("Are you sure you wish to exit? (y/n) ");
      fgets(input_str, sizeof(input_str), stdin);
      if(input_str[0] == 'y' || input_str[0] == 'Y'){
	fprintf(stderr, 
		"Exiting with potential file system errors remaining.\n");
	exit(EXIT_FAILURE);
      }
      break;
#ifdef DEBUG
    case 6:
      /* DEBUG STUFF */
      printf("Enter location to print: ");
      fgets(input_str, sizeof(input_str), stdin);
      if(!isdigit((int)input_str[0])){
	fprintf(stderr, "Invalid option.\n");
	continue;
      }
      sscanf(input_str, " %x", &location);
      printf("0x%x = (uint64)%"PRIu64"\n", location, *location);
      break;
    case 7:
      print_meminfo();
      break;
    case 8:
      printf("> ");
      fgets(input_str, sizeof(input_str), stdin);
      if(!isdigit((int)input_str[0])){
	fprintf(stderr, "Invalid option.\n");
	continue;
      }
      sscanf(input_str, " %d", &sleep_between_passes);
      break;
#endif
    default:
      fprintf(stderr, "Invalid option.\n");
      break;
    }
    fflush(NULL);
  }
 out:

  if(signal(SIGINT, sig_int) == SIG_ERR){
    fprintf(stderr, "Unable to reset signal capture for SIGINT.\n");
  }
}
/*
 * rerun_response
 *
 * This function should be called at the beginning of every fsck
 * iteration.  It keeps a static variable to track the number of
 * times the fsck has been restarted.  It prints out warnings if
 * there is a potential cyclical error, and will exist if fsck
 * is restarted too many times.
 */
static void rerun_response(void){
  static int rerun_count=0;

  if(rerun_count++){
    pp_print(PPL, "Changes made to file system.  Restarting gfs_fsck.\n");
    if((rerun_count > 10) && (rerun_count < 50)){
      pp_print(PPVH,
      "Warning::  gfs_fsck has been restarted more than 10 times.\n"
      "           There may exist an unresolvable (cyclical) error.\n");
      sleep(10);
    }
    if((rerun_count > 50) && (rerun_count < 100)){
      pp_print(PPVH,
      "Critical::  gfs_fsck has been restarted more than 50 times.\n"
      "            A cyclical error almost certainly exists.\n"
      "            Consider killing the program and contacting support.\n");
      sleep(60);
    }
    if(rerun_count > 100){
      pp_print(PPVH, 
	       "CRITICAL::  Cyclical error cannot be resolved.  Exiting.\n");
      exit(EXIT_FAILURE);
    }
  }
}


static char *print_time(char *str, time_t t){
  time_t sec=0, min=0, hours=0;
  if(t > 60){
    sec = t%60;
    t /= 60;
    if(t > 60){
      min = t%60;
      t /= 60;
      hours = t;
    } else {
      min = t;
    }
  } else {
    sec = t;
  }

  sprintf(str, "%d:%.2d:%.2d", (int)hours, (int)min, (int)sec);
  return str;
}

