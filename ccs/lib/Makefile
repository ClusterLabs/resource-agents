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
UNINSTALL = ${top_srcdir}/scripts/uninstall.pl

include ${top_srcdir}/make/defines.mk

INCLUDE = -I../include -I../common

ifeq ($(DEBUG),y)
CFLAGS+= -O2 -D_FILE_OFFSET_BITS=64 -DDEBUG -g
else
CFLAGS+= -O2 -D_FILE_OFFSET_BITS=64
endif
 
all: libccs.a

copytobin: all
 
libccs.a: libccs.o log.o
	${AR} cr libccs.a libccs.o log.o
 
libccs.o: libccs.c
	${CC} -c ${CFLAGS} ${INCLUDE} $^

log.o: ../common/log.c
	${CC} -c ${CFLAGS} ${INCLUDE} $^

install: libccs.a ccs.h
	install -d ${libdir}
	install -m644 libccs.a ${libdir}
	install -d ${incdir}
	install -m644 ccs.h ${incdir}

uninstall:
	${UNINSTALL} libccs.a ${libdir} 
	${UNINSTALL} ccs.h ${incdir} 
	rm -f /lib/libccs.a

clean:
	rm -rf *~ *.o libccs.a
 
