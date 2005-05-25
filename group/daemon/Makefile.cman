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

CFLAGS+= -g -I. -I../include/ -I../../cman/lib/ -I../lib/

TARGET=groupd

all: ${TARGET}

groupd: main.o \
	cman.o \
	joinleave.o \
	message.o \
	update.o \
	done.o \
	recover.o \
	../../cman/lib/libcman.a
	$(CC) $(LDFLAGS) -o $@ $^

main.o: main.c gd_internal.h
	$(CC) $(CFLAGS) -c -o $@ $<

joinleave.o: joinleave.c gd_internal.h
	$(CC) $(CFLAGS) -c -o $@ $<

cman.o: cman.c gd_internal.h
	$(CC) $(CFLAGS) -c -o $@ $<

message.o: message.c gd_internal.h
	$(CC) $(CFLAGS) -c -o $@ $<

update.o: update.c gd_internal.h
	$(CC) $(CFLAGS) -c -o $@ $<

recover.o: recover.c gd_internal.h
	$(CC) $(CFLAGS) -c -o $@ $<

done.o: done.c gd_internal.h
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f *.o groupd

install: groupd
	install groupd /sbin

