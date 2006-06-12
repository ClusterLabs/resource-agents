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
	-DCMAN_RELEASE_NAME=\"${RELEASE}\" -I../lib

CFLAGS+=-DDEBUG

TARGET=cman_tool

all: ${TARGET}

copytobin: all
	cp ${TARGET} ${top_srcdir}/bin

cman_tool: main.o join.o ../lib/libcman.a
	$(CC) $(LDFLAGS) -L$(ccslibdir) -o $@ $^ -L../lib -lccs

main.o: main.c cman_tool.h
	$(CC) $(CFLAGS) -c -o $@ $<

join.o: join.c cman_tool.h
	$(CC) $(CFLAGS) -c -o $@ $< -DLIBEXECDIR=\"${libexecdir}/cman\"

install: cman_tool
	install -d ${sbindir}
	install cman_tool ${sbindir}

uninstall:
	${UNINSTALL} cman_tool ${sbindir}

clean:
	rm -f *.o cman_tool

