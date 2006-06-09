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

top_srcdir=..
UNINSTALL=${top_srcdir}/scripts/uninstall.pl

include ${top_srcdir}/make/defines.mk

CFLAGS+= -g -I. -O2 \
	-idirafter ${KERNEL_SRC}/include/linux \
	-I${incdir} \
	-I${top_srcdir}/config \
	-I../include/ \
	-I../lib/ \
	-I../../cman/lib/ \
	-I../../cman/daemon/openais/trunk/include/ \
	-I../../dlm/lib/

TARGET=gfs_controld

all: ${TARGET}

copytobin: all
	cp ${TARGET} ${top_srcdir}/bin

gfs_controld: 	main.o \
		member_cman.o \
		cpg.o \
		group.o \
		plock.o \
		recover.o \
		withdraw.o \
		../../dlm/lib/libdlm_lt.a \
		../../cman/lib/libcman.a \
		../../cman/daemon/openais/trunk/lib/libcpg.a \
		../lib/libgroup.a
	$(CC) $(LDFLAGS) -o $@ $^


main.o: main.c
	$(CC) $(CFLAGS) -c -o $@ $<

member_cman.o: member_cman.c
	$(CC) $(CFLAGS) -c -o $@ $<

cpg.o: cpg.c
	$(CC) $(CFLAGS) -c -o $@ $<

recover.o: recover.c
	$(CC) $(CFLAGS) -c -o $@ $<

withdraw.o: withdraw.c
	$(CC) $(CFLAGS) -c -o $@ $<

group.o: group.c
	$(CC) $(CFLAGS) -c -o $@ $<

plock.o: plock.c
	$(CC) $(CFLAGS) -c -o $@ $<

install: gfs_controld
	install -d ${sbindir}
	install gfs_controld ${sbindir}

uninstall:
	${UNINSTALL} gfs_controld ${sbindir}

clean:
	rm -f *.o gfs_controld

