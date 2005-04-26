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

TARGET= kdbl_tool
SOURCE= main.c \
	printf.c \
	trace_profile.c

CFLAGS= -Wall -O2 -D_FILE_OFFSET_BITS=64 -I/usr/src/linux/include

all: include ${TARGET}

kdbl_tool: ${SOURCE:.c=.o}
	gcc -o $@ ${SOURCE:.c=.o}

include:
	ln -s ../../gfs-kernel/src/gfs/gfs_debug_const.h
	ln -s ../../gfs2-kernel/src/gfs2/gfs2_debug_const.h

clean:
	rm -f *.o ${TARGET} gfs_debug_const.h gfs2_debug_const.h


