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

top_srcdir=../..

CFLAGS+= -I${top_srcdir}/include \
			-I${top_srcdir}/lib

#CFLAGS+=-DDEBUG
CFLAGS+=-g

LDLIBS+= -lgulm
LOADLIBES += -L${top_srcdir}/lib

all: coretest basiclocktest

coretest: core_tests.c 
	${CC} ${CFLAGS} ${LDFLAGS} $^ ${LOADLIBES} ${LDLIBS} -o $@

basiclocktest: basic_lock_test.c 
	${CC} ${CFLAGS} ${LDFLAGS} $^ ${LOADLIBES} ${LDLIBS} -o $@


clean:
	rm -f coretest basiclocktest


