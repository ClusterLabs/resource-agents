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

CFLAGS+= -I../../dlm-kernel/src2/ -I${top_srcdir}/dlm/lib

LDFLAGS+= -L../lib


TARGET=dlm_tool

all: ${TARGET}

copytobin: all
	cp ${TARGET} ${top_srcdir}/bin

dlm_tool: main.o ioctl.o lock.o action.o
	$(CC) $(LDFLAGS) -o $@ $^ -ldlm_lt

main.o: main.c dlm_tool.h
	$(CC) $(CFLAGS) -c -o $@ $<

action.o: action.c dlm_tool.h
	$(CC) $(CFLAGS) -c -o $@ $<

ioctl.o: ioctl.c dlm_tool.h
	$(CC) $(CFLAGS) -c -o $@ $<

lock.o: lock.c dlm_tool.h
	$(CC) $(CFLAGS) -c -o $@ $<

install: dlm_tool
	install -d ${sbindir}
	install dlm_tool ${sbindir}

uninstall:
	${UNINSTALL} dlm_tool ${sbindir}

clean:
	rm -f *.o dlm_tool

