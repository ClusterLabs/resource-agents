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

#ifndef __KDBL_DOT_H__
#define __KDBL_DOT_H__


#define die(fmt, args...) \
do { \
	fprintf(stderr, "%s: ", prog_name); \
	fprintf(stderr, fmt, ##args); \
	exit(EXIT_FAILURE); \
} while (0)

#define TRUE (1)
#define FALSE (0)

#define KDBL_DEVICE ("/proc/kdbl")


/* main.c */

void print_usage();

/* printf.c */

void printf_dump(int argc, char *argv[]);

/* trace_profile.c */

void trace_print(int argc, char *argv[]);
void trace_change(int argc, char *argv[]);
void profile_dump(int argc, char *argv[]);


extern char *prog_name;


#endif  /*  __KDBL_DOT_H__  */

