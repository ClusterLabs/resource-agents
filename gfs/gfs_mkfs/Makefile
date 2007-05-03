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

TARGET= gfs_mkfs

OBJS=	device_geometry.o \
	fs_geometry.o \
	locking.o \
	main.o \
	ondisk.o \
	structures.o

CFLAGS += -DHELPER_PROGRAM -D_FILE_OFFSET_BITS=64 -DGFS_RELEASE_NAME=\"${RELEASE}\"
CFLAGS += -O2
CFLAGS += -I${gfskincdir} -I${incdir} -I${volidincdir}
CFLAGS += -I../include -I../libgfs

LDFLAGS += -L${volidlibdir} -lvolume_id
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
	(cd ${sbindir}; ln -f ${TARGET} mkfs.gfs)

uninstall:
	${UNINSTALL} ${TARGET} mkfs.gfs ${sbindir}

clean:
	rm -f *.o ${TARGET}


