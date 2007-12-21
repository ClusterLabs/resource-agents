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

TARGET= gfs_quota

OBJS=	check.o \
	main.o \
	names.o \
	ondisk.o \
	layout.o

CFLAGS += -DHELPER_PROGRAM -D_FILE_OFFSET_BITS=64
CFLAGS += -I${gfskincdir}
CFLAGS += -I$(S)/../include
CFLAGS += -I${incdir}

all: ${TARGET}

${TARGET}: ${OBJS}
	$(CC) -o $@ $^ $(LDFLAGS)

install: all
	if [ ! -d ${sbindir} ]; then \
		install -d ${sbindir}; \
	fi
	install -m755 ${TARGET} ${sbindir}

uninstall:
	${UNINSTALL} ${TARGET} ${sbindir}

clean:
	rm -f *.o ${TARGET}


