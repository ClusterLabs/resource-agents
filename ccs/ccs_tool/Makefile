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
INCLUDE= -I. -I${top_srcdir}/config -I${top_srcdir}/include -I${ccs_libdir}

ifeq ($(DEBUG),y)
CFLAGS+= -Wall -O2 -D_FILE_OFFSET_BITS=64 -D_GNU_SOURCE -DDEBUG -g \
        `xml2-config --cflags` -DCCS_RELEASE_NAME=\"${RELEASE}\"
else
CFLAGS+= -Wall -O2 -D_FILE_OFFSET_BITS=64 -D_GNU_SOURCE \
        `xml2-config --cflags` -DCCS_RELEASE_NAME=\"${RELEASE}\"
endif

LDFLAGS+= -L${ccs_libdir} `xml2-config --libs` -L${libdir}
LOADLIBES+= -lccs -lmagma -lmagmamsg -ldl

all: ccs_tool

ccs_tool: ccs_tool.c update.c upgrade.c old_parser.c
	${CC} ${CFLAGS} ${INCLUDE} -o $@ $^ ${LDFLAGS} ${LOADLIBES}

clean:
	rm -f *.o ccs_tool *~

install: all
	install -d ${sbindir}
	install ccs_tool ${sbindir}

uninstall:
	${UNINSTALL} ccs_tool ${sbindir}
