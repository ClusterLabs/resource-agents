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

TARGET= gfs_fsck

SOURCE=	\
	fs_bio.c \
	fs_bits.c \
	fs_bmap.c \
	fs_dir.c \
	fs_file.c \
	fs_inode.c \
	fs_recovery.c \
	fs_rgrp.c \
	fs_super.c \
	allocation.c \
	bitmap.c \
	initialize.c \
	interactive.c \
	lost_n_found.c \
	main.c \
	util.c \
	ondisk.c \
	pass1.c \
	pass2.c \
	pass3.c \
	pass4.c \
	pass5.c \
	pass6.c \
	pass7.c 

ifeq ($(DEBUG),y)
CFLAGS+= -DDEBUG -g -DGFS_RELEASE_NAME=\"${RELEASE}\"
endif

# The "HELPER_PROGRAM" tag keeps ondisk.c from including gfs.h
CFLAGS+= -O2 -D_FILE_OFFSET_BITS=64 -DHELPER_PROGRAM \
	-DGFS_RELEASE_NAME=\"${RELEASE}\"

INCLUDE= -I${top_srcdir}/include -I${top_srcdir}/config

ifneq (${KERNEL_SRC}, )
INCLUDE += -I${KERNEL_SRC}/include
endif


all: ${TARGET}

gfs_fsck: ${SOURCE:.c=.o}
	${CC} ${CFLAGS} ${SOURCE:.c=.o} -o $@

copytobin: all
	cp ${TARGET} ${top_srcdir}/bin

clean:
	rm -f *.o *~ ${TARGET}

