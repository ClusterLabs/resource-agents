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

LDFLAGS+= -lclulib -lxml2 -lmagmamsg -lmagma -lpthread -ldl -lccs
TARGETS=clurgmgrd clurmtabd

all: ${TARGETS}

install: clurgmgrd clurmtabd
	install -d ${sbindir}
	install clurgmgrd ${sbindir}
	install clurmtabd ${sbindir}

clurgmgrd: rg_thread.o rg_locks.o main.o groups.o rg_state.o \
		rg_queue.o members.o rg_forward.o reslist.o \
		resrules.o restree.o fo_domain.o
	$(CC) -o $@ $^ $(INCLUDE) $(CFLAGS) $(LDFLAGS)

clurmtabd: clurmtabd.o clurmtabd_lib.o
	$(CC) -o $@ $^ $(INCLUDE) $(CFLAGS) $(LDFLAGS)

clean:
	rm -f *.o $(TARGETS)

%.o: %.c
	$(CC) -c -o $@ $^ $(INCLUDE) $(CFLAGS)

#INCLUDES=-I../../include
#CFLAGS+=-DMDEBUG -DFDEBUG

#noinst_LIBRARIES = libclulib.a

#libclulib_a_SOURCES = clulog.c daemon_init.c signals.c \
#	msgsimple.c vft.c gettid.c rg_strings.c #mallocdbg.c
