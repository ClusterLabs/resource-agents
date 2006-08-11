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

TARGET= gfs2_edit

SOURCE=	\
	gfs2hex.c \
	hexedit.c

top_srcdir=..
include ${top_srcdir}/make/defines.mk

CFLAGS+= -ggdb -DHELPER_PROGRAM -D_FILE_OFFSET_BITS=64 \
	 -DGFS2_RELEASE_NAME=\"${RELEASE}\"

INCLUDE= -I${top_srcdir}/include -I${top_srcdir}/config -I../libgfs2 
LIBS=../libgfs2/

ifneq (${KERNEL_SRC}, )
# Use the kernel tree if patched, otherwise, look where cluster headers
#  should be installed
INCLUDE += $(shell if [ -e ${KERNEL_SRC}/include/linux/gfs2_ondisk.h ]; then \
		echo '-I${KERNEL_SRC}/include'; else \
		echo '-I${incdir}'; fi)
else
INCLUDE += -I${incdir}
endif

all: gfs2_edit

gfs2_edit: ${SOURCE} hexedit.h gfs2hex.h ${LIBS}/libgfs2.a
	${CC} ${CFLAGS} ${INCLUDE} -L${LIBS} ${LDFLAGS} ${SOURCE} ${LOADLIBES} ${LDLIBS} -lncurses -o $@ -lgfs2

install: all
	if [ ! -d ${sbindir} ]; then \
		install -d ${sbindir}; \
	fi
	install -m755 ${TARGET} ${sbindir}

clean:
	rm -f *.o ${TARGET}


