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

TARGET= gfs2_quota

OBJS=	main.o \
	names.o \
	check.o

CFLAGS += -DHELPER_PROGRAM -D_FILE_OFFSET_BITS=64
CFLAGS += -D_GNU_SOURCE -DGFS2_RELEASE_NAME=\"${RELEASE}\"
CFLAGS += -O2
CFLAGS += -I../include -I../libgfs2/

LDFLAGS += -L../libgfs2 -lgfs2 -lgfs2

all: ${TARGET}

${TARGET}: ${OBJS}
	$(CC) -o $@ $^ $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

install: all
	install -m 0755 ${TARGET} ${sbindir}

uninstall:
	${UNINSTALL} ${TARGET} ${sbindir}

clean:
	rm -f *.o ${TARGET}


