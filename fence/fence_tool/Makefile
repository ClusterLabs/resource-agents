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

TARGET = fence_tool

top_srcdir = ..
include ${top_srcdir}/make/defines.mk

INCLUDE = -I${top_srcdir}/include -I${top_srcdir}/config

CFLAGS+= -O2 -D_FILE_OFFSET_BITS=64 -DFENCE_RELEASE_NAME=\"${RELEASE}\"

all: ${TARGET}

fence_tool: fence_tool.o
	${CC} fence_tool.o -o fence_tool

fence_tool.o:
	${CC} ${CFLAGS} -c fence_tool.c

copytobin: all
	cp ${TARGET} ${top_srcdir}/bin

clean:
	rm -rf *~ *.o fence_tool
