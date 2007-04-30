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

TARGET= gfs2_tool

OBJS=	counters.o \
	df.o \
	layout.o \
	main.o \
	misc.o \
	ondisk.o \
	sb.o \
	tune.o \
	util.o

CFLAGS += -D_FILE_OFFSET_BITS=64 -DGFS2_RELEASE_NAME=\"${RELEASE}\"
CFLAGS += -O2 -Wall
CFLAGS += -I../include

LDFLAGS += -L../libgfs2 -lgfs2

all: ${TARGET}

${TARGET}: ${OBJS}
	$(CC) -o $@ $^ $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $^

install: all
	if [ ! -d ${sbindir} ]; then \
		install -d ${sbindir}; \
	fi
	install -m755 ${TARGET} ${sbindir}

uninstall:
	${UNINSTALL} ${TARGET} ${sbindir}

clean:
	@rm -f ${TARGET} *.o *~

distclean: clean
	@rm -f *.d cscope.* *.orig *.rej

.PHONY: all clean distclean
