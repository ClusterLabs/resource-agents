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

%{
#include <stdio.h>
#include <string.h>
#include "glvd_parser.h"
   int yyerror(char*);
   void lex_cpy_lexme (void);
   int linenumber=1;
#define YY_NO_UNPUT

%}

%x COMMENT

%%

\# { BEGIN COMMENT; }
<COMMENT>\n { linenumber++; BEGIN 0; }
<COMMENT>. {}

nodecount { return NODECOUNT; }

ok        { return OK; }
tryfailed { return TRYFAILED; }
canceled  { return CANCELED; }
badstate  { return BADSTATE; }
alreadypending { return ALREADYPENDING; }

test    { return TEST; }
end     { return END; }
lrpl    { return LRPL; }
arpl    { return ARPL; }
dropexp { return DROPEXP; }
drop    { return DROP; }
lock    { return LOCK; }
action  { return ACTION; }
cancel  { return CANCEL; }
noreaction { return NOREACTION; }

nolvb   { return NOLVB; }

exclusive { return EXCLUSIVE; }
shared    { return SHARED; }
deferred  { return DEFERRED; }
unlock    { return UNLOCK; }
holdlvb   { return HOLDLVB; }
unholdlvb  { return UNHOLDLVB; }
synclvb   { return SYNCLVB; }

noflags   { return NOFLAGS; }
docb      { return DOCB; }
nocb      { return NOCB; }
try       { return TRY; }
any       { return ANY; }
ignoreexp { return IGNOREEXP; }
cachable  { return CACHABLE; }
piority   { return PIORITY; }

\| { return FLAGOR; }

\[ { return LSBRACE; }
\] { return RSBRACE; }

[0-9]+ { yylval.num = atoi(yytext); return NUMBER; }

[a-zA-Z0-9_]+ { yylval.str = strdup(yytext); return STRING; }
[ \t] {}
\n {linenumber++;}

%%

int yywrap(void) { return 1; }

