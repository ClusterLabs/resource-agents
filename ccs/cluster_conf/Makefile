###############################################################################
###############################################################################
##
##  Copyright (C) 2005 Red Hat, Inc.  All rights reserved.
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
INCLUDE= -I. -I${top_srcdir}/config 

ifeq ($(DEBUG),y)
CFLAGS+= -Wall -O2 -D_FILE_OFFSET_BITS=64 -D_GNU_SOURCE -DDEBUG -g \
        `xml2-config --cflags` -DCCS_RELEASE_NAME=\"${RELEASE}\"
else
CFLAGS+= -Wall -O2 -D_FILE_OFFSET_BITS=64 -D_GNU_SOURCE \
        `xml2-config --cflags` -DCCS_RELEASE_NAME=\"${RELEASE}\"
endif

LDFLAGS+= `xml2-config --libs`

all: cluster_conf

cluster_conf: cluster_conf.c
	${CC} ${CFLAGS} ${INCLUDE} -o $@ $^ ${LDFLAGS} ${LOADLIBES}

clean:
	rm -f *.o cluster_conf *~

install: all
	install -d ${sbindir}
	install cluster_conf ${sbindir}

uninstall:
	${UNINSTALL} cluster_conf ${sbindir}
