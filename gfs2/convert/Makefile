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
include ${top_srcdir}/make/defines.mk

TARGET= gfs2_convert

INCLUDES=-I${top_srcdir}/include -I${top_srcdir}/config -I${top_srcdir}/libgfs2 -I/usr/include -I${KERNEL_SRC}/fs/gfs2/ -I${KERNEL_SRC}/include/

LIBS=${top_srcdir}/libgfs2

CFLAGS+=-D_FILE_OFFSET_BITS=64 -DHELPER_PROGRAM -DGFS2_RELEASE_NAME=\"2\" -Wall -g

all: ${TARGET}

install: all
	if [ ! -d ${sbindir} ]; then \
		install -d ${sbindir}; \
	fi
	install -m755 ${TARGET} ${sbindir}

gfs2_convert.o: gfs2_convert.c
	$(CC) $(INCLUDES) $(CFLAGS) -c -o gfs2_convert.o gfs2_convert.c

gfs2_convert: gfs2_convert.o ${LIBS}/libgfs2.a
	$(CC) $(INCLUDES) -L$(LIBS) $(CFLAGS) -o ${TARGET} gfs2_convert.o -lgfs2

clean:
	@rm -f *.o ${TARGET}

distclean: clean
	@rm -f {top_srcdir}/make/defines.mk
