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

#ifndef __GFS_TOOL_DOT_H__
#define __GFS_TOOL_DOT_H__


/*  Extern Macro  */

#ifndef EXTERN
#define EXTERN extern
#define INIT(X)
#else
#undef EXTERN
#define EXTERN
#define INIT(X) =X 
#endif



#define die(fmt, args...) \
do \
{ \
  fprintf(stderr, "%s: ", prog_name); \
  fprintf(stderr, fmt, ##args); \
  exit(EXIT_FAILURE); \
} \
while (0)



EXTERN char *prog_name;


/*  counters.c  */

void get_counters(int argc, char **argv);


/*  debug.c  */

void print_stack(int argc, char **argv);


/*  layout.c  */

void print_layout(int argc, char *argv[]);


/*  main.c  */

void print_usage();
void check_for_gfs(int fd, char *path);


/*  sb.c  */

void do_sb(int argc, char **argv);


/*  tune.c  */

void get_tune(int argc, char **argv);
void set_tune(int argc, char **argv);


#endif  /*  __GFS_TOOL_DOT_H__  */

