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
top_srcdir= ..
include ${top_srcdir}/make/defines.mk

TARGET= gfs_tool

SOURCE= counters.c \
	debug.c \
	layout.c \
	main.c \
	sb.c \
	tune.c \
	ondisk.c

CFLAGS= -O2 -Wall -DHELPER_PROGRAM -D_FILE_OFFSET_BITS=64 \
	-DGFS_RELEASE_NAME=\"${RELEASE}\"

INCLUDE= -I${top_srcdir}/include -I${top_srcdir}/config

ifneq (${KERNEL_SRC}, )
# Use the kernel tree if patched, otherwise, look where cluster headers
#  should be installed
INCLUDE += $(shell if [ -e ${KERNEL_SRC}/include/linux/gfs_ondisk.h ]; then \
		echo '-I${KERNEL_SRC}/include'; else \
		echo '-I${incdir}'; fi)
else
INCLUDE += -I${incdir}
endif

ifeq ($(FS_DEBUG_STACK),y)
CFLAGS += -DDEBUG_STACK
endif

all: ${TARGET}

gfs_tool: ${SOURCE}
	${CC} ${CFLAGS} ${INCLUDE} ${LDFLAGS} ${SOURCE} ${LOADLIBES} ${LDLIBS} -o $@

copytobin: ${TARGET}
	cp ${TARGET} ${top_srcdir}/bin

clean:
	rm -f *.o ${TARGET}


