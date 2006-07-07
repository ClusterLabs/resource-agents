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

CFLAGS+= -g -I${incdir} -I${top_srcdir}/config

CFLAGS+= -idirafter ${KERNEL_SRC}/include/linux \
	-I../../group/lib/ \
	-I../../ccs/lib/ \
	-I../../cman/lib/ \
	-I../include/

LDFLAGS+= -L../../cman/lib


TARGET=dlm_controld

all: ${TARGET}

copytobin: all
	cp ${TARGET} ${top_srcdir}/bin

dlm_controld:	main.o \
		member_cman.o \
		group.o \
		action.o \
		../lib/libgroup.a \
		../../ccs/lib/libccs.a
	$(CC) $(LDFLAGS) -o $@ $^ -lcman 

main.o: main.c
	$(CC) $(CFLAGS) -c -o $@ $<

member_cman.o: member_cman.c
	$(CC) $(CFLAGS) -c -o $@ $<

group.o: group.c
	$(CC) $(CFLAGS) -c -o $@ $<

action.o: action.c
	$(CC) $(CFLAGS) -c -o $@ $<

install: dlm_controld
	install -d ${sbindir}
	install dlm_controld ${sbindir}

uninstall:
	${UNINSTALL} dlm_controld ${sbindir}

clean:
	rm -f *.o dlm_controld

