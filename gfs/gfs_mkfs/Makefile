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

TARGET1= gfs_mkfs
TARGET2= mkfs.gfs

OBJS=	device_geometry.o \
	fs_geometry.o \
	locking.o \
	main.o \
	ondisk.o \
	structures.o

CFLAGS += -DHELPER_PROGRAM -D_FILE_OFFSET_BITS=64
CFLAGS += -I${gfskincdir} -I${volidincdir}
CFLAGS += -I../include -I../libgfs
CFLAGS += -I${incdir}

LDFLAGS += -L${volidlibdir} -lvolume_id
LDFLAGS += -L../libgfs -lgfs

all: depends ${TARGET1} ${TARGET2}

${TARGET1}: ${OBJS}
	$(CC) -o $@ $^ $(LDFLAGS)

${TARGET2}: ${TARGET1}
	ln -sf ${TARGET1} ${TARGET2}

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

depends:
	$(MAKE) -C ../libgfs all

install: all
	if [ ! -d ${sbindir} ]; then \
		install -d ${sbindir}; \
	fi
	install -m755 ${TARGET1} ${sbindir}
	ln -f ${sbindir}/${TARGET1} ${sbindir}/${TARGET2}

uninstall:
	${UNINSTALL} ${TARGET1} ${TARGET2} ${sbindir}

clean:
	rm -f *.o ${TARGET1} ${TARGET2}

.PHONY: all ${TARGET1}
