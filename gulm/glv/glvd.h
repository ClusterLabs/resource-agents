/******************************************************************************
*******************************************************************************
**
**  Copyright (C) 2004 Red Hat, Inc.  All rights reserved.
**
**  This copyrighted material is made available to anyone wishing to use,
**  modify, copy, or redistribute it subject to the terms and conditions
**  of the GNU General Public License v.2.
**
**
*******************************************************************************
******************************************************************************/
#ifndef __glvd_h__
#define __glvd_h__
struct glv_action {
   unsigned int line;
   enum {glv_lock, glv_act, glv_cancel, glv_dropexp} action;
   int nodeidx;
   int subid;
   char *key;
   unsigned int start;
   unsigned int stop;
   int state;
   int flags;
   char *lvb;
};
struct glv_reaction {
   unsigned int line;
   enum {glv_lrpl, glv_arpl, glv_drop} react;
   int nodeidx;
   int subid;
   char *key;
   unsigned int start;
   unsigned int stop;
   int state;
   int flags;
   int error;
   char *lvb;

   int matched;
   struct glv_reaction *next;
};
struct glv_test {
   unsigned int line;
   struct glv_action *action;
   struct glv_reaction *react;
   int allmatched;

   struct glv_test *next;
};

struct glv_testfile {
   int nodecount;
   struct glv_test *tests;
};


struct glv_testfile *parse_file(char *fl, int verbosy);
char *statestrings(int s);
char *errstring(int e);
char *flagsstr(int f);
void print_action(FILE *fp, struct glv_action *action);
void print_reaction(FILE *fp, struct glv_reaction *ract);

#endif /*__glvd_h__*/
/* vim: set ai cin et sw=3 ts=3 : */
