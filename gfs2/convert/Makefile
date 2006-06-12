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

INCLUDES=-I${top_srcdir}/include -I${top_srcdir}/config -I${top_srcdir}/libgfs2 -I/usr/include -I${KERNEL_SRC}/fs/gfs2/ -I${KERNEL_SRC}/include/

LIBS=-L${top_srcdir}/libgfs2

CFLAGS+=-D_FILE_OFFSET_BITS=64 -DHELPER_PROGRAM -DGFS2_RELEASE_NAME=\"2\" -Wall -g

all: gfs2_convert

copytobin: gfs2_convert
	cp gfs2_convert ${top_srcdir}/bin

gfs2_convert.o: gfs2_convert.c
	$(CC) $(INCLUDES) $(LIBS) $(CFLAGS) -c -o gfs2_convert.o gfs2_convert.c

gfs2_convert: gfs2_convert.o ../libgfs2/libgfs2.a
	$(CC) $(INCLUDES) $(LIBS) $(CFLAGS) -o gfs2_convert gfs2_convert.o -lgfs2

clean:
	@rm -f *.o gfs2_convert
