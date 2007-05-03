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

TARGET= gfs_grow

OBJS= main.o \
      ondisk.o

CFLAGS += -DHELPER_PROGRAM -D_FILE_OFFSET_BITS=64 -DGFS_RELEASE_NAME=\"${RELEASE}\"
CFLAGS += -I${gfskincdir} -I${incdir}
CFLAGS += -I../include -I../libgfs

LDFLAGS += -L../libgfs -lgfs

all: ${TARGET}

${TARGET}: ${OBJS}
	$(CC) -o $@ $^ $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

install: all
	if [ ! -d ${sbindir} ]; then \
		install -d ${sbindir}; \
	fi
	install -m755 ${TARGET} ${sbindir}

uninstall:
	${UNINSTALL} ${TARGET} ${sbindir}

clean:
	rm -f *.o ${TARGET}
