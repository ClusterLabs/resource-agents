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

INCLUDE= -I${top_srcdir}/include -I${top_srcdir}/config -I${gfs2kincdir}

ifneq (${KERNEL_SRC}, )
# Use the kernel tree if patched, otherwise, look where cluster headers
#  should be installed
INCLUDE += $(shell if [ -e ${KERNEL_SRC}/include/linux/gfs2_ondisk.h ]; then \
		echo '-I${KERNEL_SRC}/include'; else \
		echo '-I${incdir}'; fi)
else
INCLUDE += -I${incdir}
endif

LIBS=-L${top_srcdir}/libgfs2

.PHONY: all clean copytobin distclean

SOURCES= counters.c df.c layout.c main.c misc.c ondisk.c sb.c tune.c util.c

OBJECTS=$(subst .c,.o,${SOURCES})

TARGET= gfs2_tool

CFLAGS+= -O2 -Wall -D_FILE_OFFSET_BITS=64 -DGFS2_RELEASE_NAME=\"${RELEASE}\"

all: ${TARGET}

gfs2_tool: ${OBJECTS} ../libgfs2/libgfs2.a
	$(CC) $(CFLAGS) $(LIBS) $^ -o $@ -lgfs2

%.o: %.c
	$(CC) -c $(INCLUDE) $(CFLAGS) $< -o $@

copytobin: ${TARGET}
	cp ${TARGET} ${top_srcdir}/bin

clean:
	@rm -f ${TARGET} *.o *~

distclean: clean
	@rm -f *.d cscope.* *.orig *.rej
