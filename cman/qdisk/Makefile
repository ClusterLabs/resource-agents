###############################################################################
###############################################################################
##
##  Copyright (C) 2004-2006 Red Hat, Inc.  All rights reserved.
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

INCLUDES+=-I. -I../lib
CFLAGS +=-I${incdir} -I${top_srcdir}/config \
         -Wall -Werror -Wstrict-prototypes -Wshadow -D_GNU_SOURCE -g

TARGET=qdiskd

all: ${TARGET}

copytobin: all
	cp ${TARGET} ${top_srcdir}/bin

install: qdiskd
	install -d ${sbindir}
	install qdiskd ${sbindir}

qdiskd: disk.o crc32.o disk_util.o main.o score.o bitmap.o clulog.o \
	gettid.o ../lib/libcman.a
	gcc -o $@ $^ -lpthread -L../lib -lccs

%.o: %.c
	$(CC) -c -o $@ $^ $(INCLUDES) $(CFLAGS)

clean:
	rm -f *.o qdiskd

uninstall:
	${UNINSTALL} qdiskd ${sbindir}
