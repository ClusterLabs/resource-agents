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

TARGET= fence_rackswitch

SOURCE= do_rack.c

top_srcdir=../..

ccs_libdir=${top_srcdir}/ccs/lib

INCLUDE= -I${top_srcdir}/include -I${top_srcdir}/config


include ${top_srcdir}/make/defines.mk

CFLAGS+= -DFENCE_RELEASE_NAME=\"${RELEASE}\"

all: ${TARGET}

fence_rackswitch: ${SOURCE:.c=.o}
	${CC} ${CFLAGS} ${LDFLAGS} ${SOURCE:.c=.o} ${LOADLIBES} ${LDLIBS} -o $@

copytobin: all
	cp ${TARGET} ${top_srcdir}/bin


clean:
	rm -f *.o ${TARGET} *~

