###############################################################################
###############################################################################
##
##  Copyright (C) 2004 Red Hat, Inc.  All rights reserved.
##
##  This copyrighted material is made available to anyone wishing to use,
##  modify, copy, or redistribute it subject to the terms and conditions
##  of the GNU General Public License v.2.
##
###############################################################################
###############################################################################

LEX=flex
YACC=bison

INCLUDE=-I../lib
LIBS=-L../lib -lgulm

CFLAGS+=-g -O $(INCLUDE)

all: glvd glvc

glvc: glvc.c
	$(CC) $(CFLAGS) $(LIBS) $^ -o $@

glvd: glvd.c glvd_parser.c glvd_scanner.c
	$(CC) $(CFLAGS) $(LIBS) $^ -o $@

parsetest: glvd_parser.c glvd_scanner.c
	$(CC) $(CFLAGS) $(LIBS) -DPARSETEST $^ -o $@

glvd_parser.c: glvd_parser.y
	$(YACC) -d -o $@ glvd_parser.y

glvd_scanner.c: glvd_scanner.l
	$(LEX) -i -t glvd_scanner.l > $@

clean:
	rm -f glvd_parser.[ch] glvd_scanner.c parsetest glvd glvc

