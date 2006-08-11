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
CFLAGS +=-I${ccsincdir} -I${incdir} -I${top_srcdir}/config \
         -Wall -Werror -Wstrict-prototypes -Wshadow -D_GNU_SOURCE -g

TARGET=qdiskd mkqdisk

all: ${TARGET}

install: all
	install -d ${sbindir}
	install ${TARGET} ${sbindir}

qdiskd: disk.o crc32.o disk_util.o main.o score.o bitmap.o clulog.o \
	gettid.o proc.o ../lib/libcman.a
	gcc -o $@ $^ -lpthread -L../lib -L${ccslibdir} -lccs

mkqdisk: disk.o crc32.o disk_util.o \
	 proc.o mkqdisk.o
	gcc -o $@ $^ 

%.o: %.c
	$(CC) -c -o $@ $^ $(INCLUDES) $(CFLAGS)

clean:
	rm -f *.o ${TARGET}

uninstall:
	${UNINSTALL} ${TARGET} ${sbindir}
