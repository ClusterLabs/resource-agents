###############################################################################
###############################################################################
##
##  Copyright (C) Sistina Software, Inc.  1997-2003  All rights reserved.
##  Copyright (C) 2004 Red Hat, Inc.  All rights reserved.
##
##  This copyrighted material is made available to anyone wishing to use,
##  modify, copy, or redistribute it subject to the terms and conditions
##  of the GNU General Public License v.2.
##
###############################################################################
###############################################################################

TARGET= fence_gnbd

SOURCE= main.c

top_srcdir=../..

INCLUDE= -I${top_srcdir}/include -I${top_srcdir}/tools/gnbd_import \
	-I${top_srcdir}/config

include ${top_srcdir}/make/defines.mk

CFLAGS+= -DGNBD_RELEASE_NAME=\"${RELEASE}\"

all: ${TARGET}

fence_gnbd: ${SOURCE:.c=.o} 
	${CC} ${CFLAGS} ${LDFLAGS} ${SOURCE:.c=.o} ${LOADLIBES} ${LDLIBS} -o $@

install: all
	if [ ! -d ${sbindir} ]; then \
		install -d ${sbindir}; \
	fi
	strip ${TARGET}
	install -m755 ${TARGET} ${sbindir}

clean:
	rm -f *.o ${TARGET}
