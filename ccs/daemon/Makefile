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

INCLUDE= -I. -I../include -I../common -I${top_srcdir}/config
ifneq (${KERNEL_SRC}, )
INCLUDE += -I${KERNEL_SRC}/include/cluster
else
INCLUDE += -I/usr/include/linux/cluster
endif

ifeq ($(DEBUG),y)
CFLAGS+= -O2 -D_FILE_OFFSET_BITS=64 -D_GNU_SOURCE -DDEBUG -g \
	`xml2-config --cflags` -DCCS_RELEASE_NAME=\"${RELEASE}\"
else
CFLAGS+= -O2 -D_FILE_OFFSET_BITS=64 -D_GNU_SOURCE \
	`xml2-config --cflags` -DCCS_RELEASE_NAME=\"${RELEASE}\"
endif

LDFLAGS+= `xml2-config --libs` -lpthread

all: ccsd

copytobin: all
	cp ccsd ${top_srcdir}/bin

ccsd: ccsd.c cnx_mgr.c cman_mgr.c ../common/log.c
	${CC} ${CFLAGS} ${INCLUDE} -o $@ $^ ${LDFLAGS}

install: ccsd
	install -d ${sbindir}
	install ccsd ${sbindir}

uninstall:
	${UNINSTALL} ccsd ${sbindir}

clean:
	rm -rf *.o ccsd *~
