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

top_srcdir=../..
UNINSTALL=${top_srcdir}/scripts/uninstall.pl

include ${top_srcdir}/make/defines.mk

CFLAGS+= -g \
	-I${incdir} \
	-I${top_srcdir}/config \
	-I../../include/ \
	-I../../../group/lib/ \
	-I../../../cman/lib/ \
	-I../../../dlm/lib/ \
	-I../../../gfs-kernel/src/dlm/

TARGET=lock_dlmd

all: ${TARGET}

copytobin: all
	cp ${TARGET} ${top_srcdir}/bin

lock_dlmd: 	main.o \
		member_cman.o \
		group.o \
		plock.o \
		recover.o \
		withdraw.o \
		../../../cman/lib/libcman.a \
		../../../group/lib/libgroup.a \
		../../../dlm/lib/libdlm_lt.a
	$(CC) $(LDFLAGS) -o $@ $^

main.o: main.c
	$(CC) $(CFLAGS) -c -o $@ $<

member_cman.o: member_cman.c
	$(CC) $(CFLAGS) -c -o $@ $<

recover.o: recover.c
	$(CC) $(CFLAGS) -c -o $@ $<

withdraw.o: withdraw.c
	$(CC) $(CFLAGS) -c -o $@ $<

group.o: group.c
	$(CC) $(CFLAGS) -c -o $@ $<

plock.o: plock.c
	$(CC) $(CFLAGS) -c -o $@ $<

install: lock_dlmd 
	install -d ${sbindir}
	install lock_dlmd ${sbindir}

uninstall:
	${UNINSTALL} lock_dlmd ${sbindir}

clean:
	rm -f *.o lock_dlmd

