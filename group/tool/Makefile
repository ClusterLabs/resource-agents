###############################################################################
###############################################################################
##
##  Copyright (C) 2005 Red Hat, Inc.  All rights reserved.
##
##  This copyrighted material is made available to anyone wishing to use,
##  modify, copy, or redistribute it subject to the terms and conditions
##  of the GNU General Public License v.2.
##
###############################################################################
###############################################################################

CFLAGS+= -g -I. -I../lib/ -I../daemon/

TARGET=group_tool

all: ${TARGET}

group_tool: main.o \
	../lib/libgroup.a
	$(CC) $(LDFLAGS) -o $@ $^

main.o: main.c
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f *.o group_tool

install: group_tool
	install group_tool ${sbindir}

