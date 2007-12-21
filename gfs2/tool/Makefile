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

include ../../make/defines.mk
include $(OBJDIR)/make/cobj.mk

TARGET= gfs2_tool

OBJS=	counters.o \
	df.o \
	layout.o \
	main.o \
	misc.o \
	ondisk.o \
	sb.o \
	tune.o

CFLAGS += -D_FILE_OFFSET_BITS=64
CFLAGS += -I${KERNEL_SRC}/fs/gfs2/ -I${KERNEL_SRC}/include/
CFLAGS += -I$(S)/../include -I$(S)/../libgfs2
CFLAGS += -I${incdir}

LDFLAGS += -L../libgfs2 -lgfs2

all: depends ${TARGET}

${TARGET}: ${OBJS}
	$(CC) -o $@ $^ $(LDFLAGS)

depends:
	$(MAKE) -C ../libgfs2 all

install: all
	if [ ! -d ${sbindir} ]; then \
		install -d ${sbindir}; \
	fi
	install -m755 ${TARGET} ${sbindir}

uninstall:
	${UNINSTALL} ${TARGET} ${sbindir}

clean:
	rm -f ${TARGET} *.o *~ *.d cscope.* *.orig *.rej

.PHONY: all ${TARGET}
