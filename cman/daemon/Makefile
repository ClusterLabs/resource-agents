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
UNINSTALL=${top_srcdir}/scripts/uninstall.pl

include ${top_srcdir}/make/defines.mk

CFLAGS+= -g -I${incdir} -I${top_srcdir}/config \
	-DCMAN_RELEASE_NAME=\"${RELEASE}\"

ifneq (${KERNEL_SRC}, )
# Use the kernel tree if patched, otherwise, look where cluster headers
#  should be installed
CFLAGS += $(shell if [ -d ${KERNEL_SRC}/include/cluster ]; then \
		echo '-I${KERNEL_SRC}/include/cluster'; else \
		echo '-I${incdir}/cluster'; fi)
else
CFLAGS += -I${incdir}/cluster
endif


TARGET=cmand

all: ${TARGET}

copytobin: all
	cp ${TARGET} ${top_srcdir}/bin

cmand:	daemon.o cnxman.o membership.o config.o logging.o ../lib/libcman.a
	$(CC) $(CFLAGS) -o $@ $^ -L../lib -lcman -lpthread


daemon.o: daemon.c cnxman-private.h cnxman-socket.h cnxman.h list.h config.h
	$(CC) $(CFLAGS) -c -o $@ $<

cnxman.o: cnxman.c cnxman-private.h cnxman-socket.h cnxman.h list.h config.h
	$(CC) $(CFLAGS) -c -o $@ $<

membership.o: membership.c cnxman-private.h cnxman-socket.h cnxman.h list.h config.h
	$(CC) $(CFLAGS) -c -o $@ $<

config.o: config.c config.h
	$(CC) $(CFLAGS) -c -o $@ $<

logging.o: logging.c
	$(CC) $(CFLAGS) -c -o $@ $<

install: cmand
	install -d ${sbindir}
	install cmand ${sbindir}

uninstall:
	${UNINSTALL} cmand ${sbindir}

clean:
	rm -f *.o cmand

