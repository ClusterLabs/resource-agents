###############################################################################
###############################################################################
##
##  Copyright (C) Sistina Software, Inc.  1997-2003  All rights reserved.
##  Copyright (C) 2004 Red Hat, Inc.  All rights reserved.
##  
##  This copyrighted material is made available to anyone wishing to use,
##  modify, copy, or redistribute it subject to the terms and conditions
##  of the GNU General Public License v.2.
##
###############################################################################
###############################################################################

top_srcdir=..
include ${top_srcdir}/make/defines.mk
UNINSTALL=${top_srcdir}/scripts/uninstall.pl

INCLUDE= -I${top_srcdir}/include
TARGET=libiddev.a
CFLAGS= ${INCLUDE} -Wall -O2 -D_FILE_OFFSET_BITS=64 -DHELPER_PROGRAM

all: ${TARGET}

libiddev.o: iddev.o size.o iddev.h
	${LD} -o $@ -r ${LDFLAGS} iddev.o size.o ${LOADLIBES} ${LDLIBS} --retain-symbols-file=exported_symbols.sym

libiddev.a: libiddev.o
	${AR} cr $@ libiddev.o

identify_device: libiddev.a identify_device.o
	${CC} -o $@ identify_device.o libiddev.a

copytobin: all

clean:
	rm -f *.o *.po *.a *~ ${TARGET} identify_device

install: libiddev.a iddev.h
	install -d ${libdir}
	install -m644 libiddev.a ${libdir}
	install -d ${incdir}
	install -m644 iddev.h ${incdir}

uninstall:
	${UNINSTALL} iddev.h ${incdir}
	${UNINSTALL} libiddev.a ${libdir}
