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

CFLAGS+= -g -I${incdir} -DCMAN_RELEASE_NAME=\"${RELEASE}\"

CFLAGS+= -g -Werror -Wstrict-prototypes -Wshadow -fPIC -D_GNU_SOURCE


TARGETS=libclulib.a

all: ${TARGETS}

install: all

uninstall:

libclulib.a: clulog.o daemon_init.o signals.o msgsimple.o \
		vft.o gettid.o rg_strings.o # alloc.o
	${AR} cru $@ $^
	ranlib $@

%.o: %.c
	$(CC) -c -o $@ $^ $(INCLUDE) $(CFLAGS)

clean:
	rm -f *.o $(TARGETS)
