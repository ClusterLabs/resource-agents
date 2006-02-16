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

CFLAGS+= -g -I. -I../include/ -I../../cman/lib/ -I../lib/ -I../../cman/daemon/openais/trunk/include/

TARGET=groupd

all: ${TARGET}

groupd: app.o \
	cpg.o \
	cman.o \
	joinleave.o \
	main.o \
	../../cman/lib/libcman.a \
	../../cman/daemon/openais/trunk/lib/libcpg.a
	$(CC) $(LDFLAGS) -o $@ $^

app.o: app.c gd_internal.h
	$(CC) $(CFLAGS) -c -o $@ $<

cman.o: cman.c gd_internal.h
	$(CC) $(CFLAGS) -c -o $@ $<

cpg.o: cpg.c gd_internal.h
	$(CC) $(CFLAGS) -c -o $@ $<

joinleave.o: joinleave.c gd_internal.h
	$(CC) $(CFLAGS) -c -o $@ $<

main.o: main.c gd_internal.h
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f *.o groupd

install: groupd
	install groupd /sbin

