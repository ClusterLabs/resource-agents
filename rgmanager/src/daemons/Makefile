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

top_srcdir=../..
UNINSTALL=${top_srcdir}/scripts/uninstall.pl

include ${top_srcdir}/make/defines.mk
INCLUDE += -I $(top_srcdir)/include

CFLAGS+= -g -I${incdir} -I/usr/include/libxml2 -L${libdir}

CFLAGS+= -g -Werror -Wstrict-prototypes -Wshadow -fPIC -D_GNU_SOURCE

CFLAGS+= -L ../clulib 

LDFLAGS+= -lclulib -lxml2 -lmagmamsg -lmagma -lpthread -ldl
TARGETS=clurgmgrd clurmtabd

all: ${TARGETS}

install: all
	install -d ${sbindir}
	install clurgmgrd ${sbindir}
	install clurmtabd ${sbindir}

uninstall:
	${UNINSTALL} clurgmgrd ${sbindir}
	${UNINSTALL} clurmtabd ${sbindir}

clurgmgrd: rg_thread.o rg_locks.o main.o groups.o rg_state.o \
		rg_queue.o members.o rg_forward.o reslist.o \
		resrules.o restree.o fo_domain.o
	$(CC) -o $@ $^ $(INCLUDE) $(CFLAGS) $(LDFLAGS) -lccs

#
# Our test program links against the local allocator so that
# we can see if our program is leaking memory during XML parsing, tree
# delta calculations, building/teardown of resource lists, etc.
# If it's leaking memory, the 'make check' will fail.  Also, we can
# use it to test known-good configurations for regressions.
#
# The data in the 'tests' directory is hand-crafted; so running 'gentests.sh'
# will require that the developer hand-verify the correctness of the
# resulting output prior to committing back to CVS.
#
# This is NOT meant to be an installed binary.  Rather, RPMs and/or other
# packages should run 'make check' as part of the build process.
#
rg_test: rg_locks-noccs.o test-noccs.o reslist-noccs.o \
		resrules.o restree-noccs.o fo_domain-noccs.o
	$(CC) -o $@ $^ $(INCLUDE) $(CFLAGS) -llalloc $(LDFLAGS)

clurmtabd: clurmtabd.o clurmtabd_lib.o
	$(CC) -o $@ $^ $(INCLUDE) $(CFLAGS) $(LDFLAGS)

clean:
	rm -f *.o $(TARGETS) rg_test
	rm -f tests/*.out*

check: rg_test
	cd tests && ./runtests.sh

%.o: %.c
	$(CC) -c -o $@ $^ $(INCLUDE) $(CFLAGS)

%-noccs.o: %.c
	$(CC) -c -o $@ $^ $(INCLUDE) $(CFLAGS) -DNO_CCS

