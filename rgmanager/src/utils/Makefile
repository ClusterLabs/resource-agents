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

CFLAGS+= -g -I${incdir}

CFLAGS+= -g -Werror -Wstrict-prototypes -Wshadow -fPIC -D_GNU_SOURCE

CFLAGS+= -L ../clulib -lclulib
TARGETS=clubufflush clufindhostname clustat clusvcadm clulog

all: ${TARGETS}

cluarp: cluarp.o
	$(CC) -c -o $@ $^ $(INLUDE) $(CCFLAGS)

clulog: clulog.o
	$(CC) -c -o $@ $^ $(INLUDE) $(CCFLAGS)

clubufflush: clubufflush.o
	$(CC) -c -o $@ $^ $(INLUDE) $(CCFLAGS)

clufindhostname: clufindhostname.o
	$(CC) -c -o $@ $^ $(INLUDE) $(CCFLAGS)

clustat: clustat.o
	$(CC) -c -o $@ $^ $(INLUDE) $(CCFLAGS)

clusvcadm: clusvcadm.o
	$(CC) -c -o $@ $^ $(INLUDE) $(CCFLAGS)

clean:
	rm -f *.o $(TARGETS)

%.o: %.c
	$(CC) -c -o $@ $^ $(INCLUDE) $(CFLAGS)
