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

ccs_libdir=${top_srcdir}/lib
INCLUDE+= -I. -I${top_srcdir}/include -I${ccs_libdir} -I${top_srcdir}/config

ifeq ($(DEBUG),y)
CFLAGS+= -O2 -D_FILE_OFFSET_BITS=64 -D_GNU_SOURCE -DDEBUG -g
else
CFLAGS+= -O2 -D_FILE_OFFSET_BITS=64 -D_GNU_SOURCE
endif

LDFLAGS+= -L${ccs_libdir}
LOADLIBES+= -lccs

all: ccs_test

copytobin: all
	cp ccs_test ${top_srcdir}/bin

ccs_test: ccs_test.c ${ccs_libdir}/libccs.a
	${CC} ${CFLAGS} ${INCLUDE} ccs_test.c ${LDFLAGS} ${LOADLIBES} ${LDLIBS}-o $@

${ccs_libdir}/libccs.a:
	cd ${ccs_libdir} && ${MAKE} libccs.a

install: ccs_test
	install -d ${sbindir}
	install ccs_test ${sbindir}

uninstall:
	${UNINSTALL} ccs_test ${sbindir}

clean:
	rm -rf *.o ccs_test *~
