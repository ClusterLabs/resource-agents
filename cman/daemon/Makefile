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

top_srcdir=..
UNINSTALL=${top_srcdir}/scripts/uninstall.pl

include ${top_srcdir}/make/defines.mk

CFLAGS+= -g -I${ccsincdir} -I${incdir} -I${top_srcdir}/config \
	-DCMAN_RELEASE_NAME=\"${RELEASE}\" -I../lib -DDEBUG

TARGET=cmand

all: ${TARGET}

copytobin: all
	cp ${TARGET} ${top_srcdir}/bin

cmand:	daemon.o cnxman.o membership.o config.o logging.o barrier.o commands.c ../lib/libcman.a
	$(CC) $(CFLAGS) -o $@ $^ -L../lib -L${ccslibdir} -lccs -lcman -lpthread


daemon.o: daemon.c cnxman-private.h cnxman-socket.h cnxman.h list.h config.h
	$(CC) $(CFLAGS) -c -o $@ $<

cnxman.o: cnxman.c cnxman-private.h cnxman-socket.h cnxman.h list.h config.h
	$(CC) $(CFLAGS) -c -o $@ $<

membership.o: membership.c cnxman-private.h cnxman-socket.h cnxman.h list.h config.h
	$(CC) $(CFLAGS) -c -o $@ $<

config.o: config.c config.h
	$(CC) $(CFLAGS) -c -o $@ $<

logging.o: logging.c
	$(CC) $(CFLAGS) -c -o $@ $<

install: cmand
	install -d ${sbindir}
	install cmand ${sbindir}

uninstall:
	${UNINSTALL} cmand ${sbindir}

clean:
	rm -f *.o cmand

