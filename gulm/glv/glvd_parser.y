%{
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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <stdint.h>
#include <netinet/in.h>

#include "libgulm.h"
#include "glvd.h"

extern char *yytext;
extern FILE *yyin;
extern int linenumber;
int errors=0;

struct glv_testfile *root=NULL;

int yyerror(char*);
int yylex();

%}

%union
{
   int num;
   char *str;
   struct glv_test *test;
   struct glv_action *action;
   struct glv_reaction *react;
}

%token NODECOUNT

%token OK
%token TRYFAILED
%token CANCELED
%token BADSTATE
%token ALREADYPENDING

%token TEST
%token END
%token LRPL
%token ARPL
%token DROPEXP
%token DROP
%token LOCK
%token ACTION
%token CANCEL
%token NOREACTION

%token NOLVB

%token EXCLUSIVE
%token SHARED
%token DEFERRED
%token UNLOCK

%token HOLDLVB
%token UNHOLDLVB
%token SYNCLVB

%token NOFLAGS
%token DOCB
%token NOCB
%token TRY
%token ANY
%token IGNOREEXP
%token CACHABLE
%token PIORITY
%token FLAGOR

%token LSBRACE
%token RSBRACE

%token <str> STRING
%token <num> NUMBER

%type <test> test
%type <test> tests
%type <action> action
%type <react> reaction
%type <react> reactions
%type <num> nodeidx
%type <num> subid
%type <str> lvb
%type <num> state
%type <num> actname
%type <num> flags
%type <num> iflags
%type <num> flag
%type <num> errorcode

%start testfile
%%

testfile : defines tests
         ;

defines : defines define
        | define
        ;
define : nodecount
       ;

nodecount : NODECOUNT NUMBER
           { root->nodecount = $2; }
          ;

/* chain these onto the end of the list.
 * keeps the list in the same order as the file.
 * which given what we're doing is rather important.
 */
tests : tests test
       {
         $1->next = $2;
         $$ = $2;
       }
      | test
       { root->tests = $1; }
      ;

test : TEST action reactions END
        { struct glv_test *tmp;
        tmp = malloc(sizeof(struct glv_test));
        tmp->line = linenumber;
        tmp->action = $2;
        tmp->react = $3;
        tmp->allmatched = 0;
        tmp->next = NULL;
        $$ = tmp;
      }
     ;

action : LOCK nodeidx subid STRING state flags lvb
        { struct glv_action *tmp;
          tmp = malloc(sizeof(struct glv_action));
          tmp->line = linenumber;
          tmp->action = glv_lock;
          tmp->nodeidx = $2;
          tmp->subid = $3;
          tmp->key = $4;
          tmp->start = 0;
          tmp->stop = ~0;
          tmp->state = $5;
          tmp->flags = $6;
          tmp->lvb = $7;
          $$ = tmp;
        }
       | LOCK nodeidx subid STRING LSBRACE NUMBER NUMBER RSBRACE state flags lvb
        { struct glv_action *tmp;
          tmp = malloc(sizeof(struct glv_action));
          tmp->line = linenumber;
          tmp->action = glv_lock;
          tmp->nodeidx = $2;
          tmp->subid = $3;
          tmp->key = $4;
          tmp->start = $6;
          tmp->stop = $7;
          tmp->state = $9;
          tmp->flags = $10;
          tmp->lvb = $11;
          $$ = tmp;
        }
       | ACTION nodeidx subid STRING actname lvb
        { struct glv_action *tmp;
          tmp = malloc(sizeof(struct glv_action));
          tmp->line = linenumber;
          tmp->action = glv_act;
          tmp->nodeidx = $2;
          tmp->subid = $3;
          tmp->key = $4;
          tmp->start = 0;
          tmp->stop = 0;
          tmp->state = $5;
          tmp->flags = 0;
          tmp->lvb = $6;
          $$ = tmp;
        }
       | CANCEL nodeidx subid STRING
        { struct glv_action *tmp;
          tmp = malloc(sizeof(struct glv_action));
          tmp->line = linenumber;
          tmp->action = glv_cancel;
          tmp->nodeidx = $2;
          tmp->subid = $3;
          tmp->key = $4;
          tmp->start = 0;
          tmp->stop = 0;
          tmp->state = 0;
          tmp->flags = 0;
          tmp->lvb = NULL;
          $$ = tmp;
        }
       | DROPEXP nodeidx STRING STRING
        { struct glv_action *tmp;
          tmp = malloc(sizeof(struct glv_action));
          tmp->line = linenumber;
          tmp->action = glv_dropexp;
          tmp->nodeidx = $2;
          tmp->subid = 0;
          tmp->key = $3;
          tmp->start = 0;
          tmp->stop = 0;
          tmp->state = 0;
          tmp->flags = 0;
          tmp->lvb = $4;
          $$ = tmp;
        }
       ;

reactions : reactions reaction
             {
               $2->next = $1;
               $$ = $2;
             }
          | reaction
          | NOREACTION
          { $$ = NULL; }
          ;
reaction : LRPL nodeidx subid STRING state flags errorcode lvb
          { struct glv_reaction *tmp;
            tmp = malloc(sizeof(struct glv_reaction));
            tmp->line = linenumber;
            tmp->react = glv_lrpl;
            tmp->nodeidx = $2;
            tmp->subid = $3;
            tmp->key = $4;
            tmp->start = 0;
            tmp->stop = ~0;
            tmp->state = $5;
            tmp->flags = $6;
            tmp->error = $7;
            tmp->lvb = $8;
            tmp->matched = 0;
            tmp->next = NULL;
            $$ = tmp;
          }
         | LRPL nodeidx subid STRING LSBRACE NUMBER NUMBER RSBRACE state flags errorcode lvb
          { struct glv_reaction *tmp;
            tmp = malloc(sizeof(struct glv_reaction));
            tmp->line = linenumber;
            tmp->react = glv_lrpl;
            tmp->nodeidx = $2;
            tmp->subid = $3;
            tmp->key = $4;
            tmp->start = $6;
            tmp->stop = $7;
            tmp->state = $9;
            tmp->flags = $10;
            tmp->error = $11;
            tmp->lvb = $12;
            tmp->matched = 0;
            tmp->next = NULL;
            $$ = tmp;
          }
         | ARPL nodeidx subid STRING actname errorcode
          { struct glv_reaction *tmp;
            tmp = malloc(sizeof(struct glv_reaction));
            tmp->line = linenumber;
            tmp->react = glv_arpl;
            tmp->nodeidx = $2;
            tmp->subid = $3;
            tmp->key = $4;
            tmp->start = 0;
            tmp->stop = 0;
            tmp->state = $5;
            tmp->flags = 0;
            tmp->error = $6;
            tmp->lvb = NULL;
            tmp->matched = 0;
            tmp->next = NULL;
            $$ = tmp;
          }
         | DROP nodeidx subid STRING state
          { struct glv_reaction *tmp;
            tmp = malloc(sizeof(struct glv_reaction));
            tmp->line = linenumber;
            tmp->react = glv_drop;
            tmp->nodeidx = $2;
            tmp->subid = $3;
            tmp->key = $4;
            tmp->start = 0;
            tmp->stop = 0;
            tmp->state = $5;
            tmp->flags = 0;
            tmp->error = 0;
            tmp->lvb = NULL;
            tmp->matched = 0;
            tmp->next = NULL;
            $$ = tmp;
          }
         ;

nodeidx : NUMBER
        ;

subid : NUMBER
      ;

state : EXCLUSIVE
       { $$ = lg_lock_state_Exclusive; }
      | SHARED
       { $$ = lg_lock_state_Shared; }
      | DEFERRED
       { $$ = lg_lock_state_Deferred; }
      | UNLOCK
       { $$ = lg_lock_state_Unlock; }
      ;

actname : HOLDLVB
         { $$ = lg_lock_act_HoldLVB; }
        | UNHOLDLVB
         { $$ = lg_lock_act_UnHoldLVB; }
        | SYNCLVB
         { $$ = lg_lock_act_SyncLVB; }
        ;

lvb : NOLVB
     { $$ = NULL; }
    | STRING
    ;

flags : NOFLAGS
       { $$ = 0; }
      | iflags
      ;
iflags : iflags FLAGOR flag
        { $$ = $1 | $3; }
       | flag
       ;
flag : DOCB
       { $$ = lg_lock_flag_DoCB; }
     | NOCB
       { $$ = lg_lock_flag_NoCallBacks; }
     | TRY
       { $$ = lg_lock_flag_Try; }
     | ANY
       { $$ = lg_lock_flag_Any; }
     | IGNOREEXP
       { $$ = lg_lock_flag_IgnoreExp; }
     | CACHABLE
       { $$ = lg_lock_flag_Cachable; }
     | PIORITY
       { $$ = lg_lock_flag_Piority; }
     ;

errorcode : OK
       { $$ = lg_err_Ok; }
      | TRYFAILED
       { $$ = lg_err_TryFailed; }
      | BADSTATE
       { $$ = lg_err_BadStateChg; }
      | CANCELED
       { $$ = lg_err_Canceled; }
      | ALREADYPENDING
       { $$ = lg_err_AlreadyPend; }
      ;

%%

char *statestrings(int s)
{
   switch(s) {
      case lg_lock_state_Exclusive: return "exclusive"; break;
      case lg_lock_state_Shared: return "shared"; break;
      case lg_lock_state_Deferred: return "deferred"; break;
      case lg_lock_state_Unlock: return "unlock"; break;
      case lg_lock_act_HoldLVB: return "holdlvb"; break;
      case lg_lock_act_UnHoldLVB: return "unholdlvb"; break;
      case lg_lock_act_SyncLVB: return "synclvb"; break;
   }
}
char *errstring(int e)
{
   switch(e) {
      case lg_err_Ok: return "ok"; break;
      case lg_err_TryFailed: return "tryfailed"; break;
      case lg_err_BadStateChg: return "badstate"; break;
      case lg_err_Canceled: return "canceled"; break;
      case lg_err_AlreadyPend: return "alreadypeding"; break;
   }
}
char *flagsstr(int f)
{
   static char buffy[64];
   int l;
   buffy[0] = '\0';
   if( f & lg_lock_flag_Try) strcat(buffy, "try|");
   if( f & lg_lock_flag_DoCB ) strcat(buffy, "docb|");
   if( f & lg_lock_flag_NoCallBacks) strcat(buffy, "nocb|");
   if( f & lg_lock_flag_Any) strcat(buffy, "any|");
   if( f & lg_lock_flag_IgnoreExp) strcat(buffy, "ignoreexp|");
   if( f & lg_lock_flag_Cachable) strcat(buffy, "cachable|");
   if( f & lg_lock_flag_Piority) strcat(buffy, "piority|");
   l = strlen(buffy);
   if( l == 0 ) strcpy(buffy, "noflags");
   if( buffy[l-1] == '|' ) buffy[l-1] = '\0';

   return buffy;
}

void print_action(FILE *fp, struct glv_action *action)
{
   switch(action->action) {
      case glv_lock:
         if( action->start != 0 || action->stop != ~0 ) {
         fprintf(fp, "  lock %d %d %s [%u %u] %s %s %s\n", action->nodeidx,
                action->subid, action->key,
                action->start, action->stop,
                statestrings(action->state),
                flagsstr(action->flags),
                action->lvb==NULL?"nolvb":action->lvb);
         }else{
         fprintf(fp, "  lock %d %d %s %s %s %s\n", action->nodeidx,
                action->subid, 
                action->key, statestrings(action->state),
                flagsstr(action->flags),
                action->lvb==NULL?"nolvb":action->lvb);
         }
         break;
      case glv_act:
         fprintf(fp, "  action %d %d %s %s %s\n", action->nodeidx,
                action->subid, 
                action->key, statestrings(action->state),
                action->lvb==NULL?"nolvb":action->lvb);
         break;
      case glv_cancel:
         fprintf(fp, "  cancel %d %d %s\n", action->nodeidx,
                action->subid, action->key);
         break;
      case glv_dropexp:
         fprintf(fp, "  dropexp %d %s %s\n", action->nodeidx,
                action->key,
                action->lvb==NULL?"nolvb":action->lvb);
         break;
   }
}

void print_reaction(FILE *fp, struct glv_reaction *ract)
{
   switch(ract->react) {
      case glv_lrpl:
         if( ract->start != 0 || ract->stop != ~0 ) {
         fprintf(fp, "  lrpl %d %d %s [%u %u] %s %s %s %s\n", ract->nodeidx,
                ract->subid, ract->key,
                ract->start, ract->stop,
                statestrings(ract->state),
                flagsstr(ract->flags), errstring(ract->error),
                ract->lvb==NULL?"nolvb":ract->lvb);
         }else{
         fprintf(fp, "  lrpl %d %d %s %s %s %s %s\n", ract->nodeidx,
                ract->subid,
                ract->key, statestrings(ract->state),
                flagsstr(ract->flags), errstring(ract->error),
                ract->lvb==NULL?"nolvb":ract->lvb);
         }
         break;
      case glv_arpl:
         fprintf(fp, "  arpl %d %d %s %s %s\n", ract->nodeidx,
                ract->subid,
                ract->key, statestrings(ract->state),
                errstring(ract->error));
         break;
      case glv_drop:
         fprintf(fp, "  drop %d %d %s %s\n", ract->nodeidx,
                ract->subid,
                ract->key, statestrings(ract->state));
         break;
   }
}

void show_it(void)
{
   struct glv_test *tst;
   struct glv_reaction *ract;

   printf("nodecount %d\n", root->nodecount);

   for(tst=root->tests; tst != NULL; tst = tst->next) {
      printf("test\n");
      print_action(stdout, tst->action);
      if( tst->react == NULL ) {
         printf("  noreaction\n");
      }else{
         for(ract=tst->react; ract != NULL; ract = ract->next ) {
            print_reaction(stdout, ract);
         }
      }
      printf("end\n\n");
   }
}

#ifdef PARSETEST
int main(int argc, char **argv)
{
   argc--; argv++;
   if (argc > 0)
      yyin = fopen(argv[0],"r");
   else
      yyin = stdin;

   root = malloc(sizeof(struct glv_testfile));
   root->nodecount = 0;
   root->tests = NULL;

   yyparse();

   fclose(yyin);

   if( errors != 0)
      fprintf(stderr, "There were %d errors found in '%s'\n",errors,argv[0]);
   else
      show_it();

   return 0;
}
#else /*PARSETEST*/
struct glv_testfile *parse_file(char *fl, int verbosy)
{
   if(strncmp("-", fl, 1)==0)
      yyin = stdin;
   else
      yyin = fopen(fl, "r");

   root = malloc(sizeof(struct glv_testfile));
   root->nodecount = 0;
   root->tests = NULL;

   yyparse();

   fclose(yyin);

   if(errors != 0) {
      fprintf(stderr," %d Errors were found.\n", errors);
      return NULL;
   }else {
      if(verbosy > 4) show_it();
      return root;
   }
}

#endif /*PARSETEST*/

int yyerror (char *mesg)
{
   errors++;
   fprintf(stderr,"Error found in line %d\n",linenumber);
   fprintf(stderr,"  %s\n",mesg);
   return 0;
}

/* vim: set ai et sw=3 ts=3 : */
