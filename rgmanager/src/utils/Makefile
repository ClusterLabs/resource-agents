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

CFLAGS+= -g -I${incdir}

CFLAGS+= -g -Wstrict-prototypes -Wshadow -fPIC -D_GNU_SOURCE

CFLAGS+= -DPACKAGE_VERSION=\"${RELEASE}\"

LDFLAGS+= -L${libdir} -L../../../cman/lib -L../../../ccs/lib -lcman -lpthread -ldl -lncurses -L../clulib -lclulib -lccs
TARGETS=clubufflush clufindhostname clustat clusvcadm clulog clunfslock

all: ${TARGETS}

install: all
	install -d ${sbindir}
	install $(TARGETS) ${sbindir}

uninstall:
	${UNINSTALL} $(TARGETS) ${sbindir}

cluarp: cluarp.o
	$(CC) -o $@ $^ $(INLUDE) $(CFLAGS) $(LDFLAGS)

clulog: clulog.o
	$(CC) -o $@ $^ $(INLUDE) $(CFLAGS) $(LDFLAGS)

clubufflush: clubufflush.o
	$(CC) -o $@ $^ $(INLUDE) $(CFLAGS) $(LDFLAGS)

clufindhostname: clufindhostname.o
	$(CC) -o $@ $^ $(INLUDE) $(CFLAGS)

clustat: clustat.o
	$(CC) -o $@ $^ $(INLUDE) $(CFLAGS) $(LDFLAGS)

clusvcadm: clusvcadm.o
	$(CC) -o $@ $^ $(INLUDE) $(CFLAGS) $(LDFLAGS)

clunfslock: clunfslock.sh
	cp clunfslock.sh clunfslock
	chmod 755 clunfslock

clean:
	rm -f *.o $(TARGETS)

%.o: %.c
	$(CC) -c -o $@ $^ $(INCLUDE) $(CFLAGS)
