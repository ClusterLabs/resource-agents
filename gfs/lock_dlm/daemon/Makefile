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

CFLAGS+= -g -I${incdir} -I../../../group/daemon/ -I${top_srcdir}/config -I../../../cman/lib/ -I../../include/


TARGET=lock_dlmd

all: ${TARGET}

copytobin: all
	cp ${TARGET} ${top_srcdir}/bin

lock_dlmd: main.o member_cman.o recover.o ../../../cman/lib/libcman.a
	$(CC) $(LDFLAGS) -o $@ $^

main.o: main.c
	$(CC) $(CFLAGS) -c -o $@ $<

member_cman.o: member_cman.c
	$(CC) $(CFLAGS) -c -o $@ $<

recover.o: recover.c
	$(CC) $(CFLAGS) -c -o $@ $<

install: lock_dlmd 
	install -d ${sbindir}
	install lock_dlmd ${sbindir}

uninstall:
	${UNINSTALL} lock_dlmd ${sbindir}

clean:
	rm -f *.o lock_dlmd

