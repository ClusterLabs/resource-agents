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
INCLUDE += -I $(top_srcdir)/include -I $(top_srcdir)/../cman/lib -I $(top_srcdir)/../ccs/lib -I $(top_srcdir)/../dlm/lib

CFLAGS+= -g -I${incdir} -DCMAN_RELEASE_NAME=\"${RELEASE}\"

CFLAGS+= -g -Wstrict-prototypes -Wshadow -fPIC -D_GNU_SOURCE


TARGETS=libclulib.a liblalloc.a msgtest 

all: ${TARGETS}

install: all

uninstall:

msgtest: msgtest.o libclulib.a
	gcc -o msgtest msgtest.o -L$(top_srcdir)/../cman/lib -lcman -L. -lclulib -llalloc -lpthread

libclulib.a: clulog.o daemon_init.o signals.o msgsimple.o \
		gettid.o rg_strings.o message.o members.o fdops.o \
		lock.o cman.o vft.o msg_cluster.o msg_socket.o
	${AR} cru $@ $^
	ranlib $@

liblalloc.a: alloc.o
	${AR} cru $@ $^
	ranlib $@

%.o: %.c
	$(CC) -c -o $@ $^ $(INCLUDE) $(CFLAGS)

clean:
	rm -f *.o $(TARGETS)
