###############################################################################
###############################################################################
##
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

TARGET= ccsd

OBJS=	ccsd.o \
	cnx_mgr.o \
	cluster_mgr.o \
	misc.o \
	globals.o

CFLAGS += -D_FILE_OFFSET_BITS=64 -D_GNU_SOURCE
CFLAGS += -I${cmanincdir} `xml2-config --cflags`
CFLAGS += -I$(S) -I$(S)/../include -I$(S)/../common
CFLAGS += -I${incdir}

LDFLAGS += -L${cmanlibdir} -lcman -L${ccslibdir} -lccs
LDFLAGS += -L${libdir} `xml2-config --libs` -lpthread -ldl

all: depends ${TARGET}

${TARGET}: ${OBJS}
	$(CC) -o $@ $^ $(LDFLAGS)

depends:
	 $(MAKE) -C ../lib all

install: all
	install -d ${sbindir}
	install ${TARGET} ${sbindir}

uninstall:
	${UNINSTALL} ${TARGET} ${sbindir}

clean:
	rm -rf *.o ${TARGET} *~

.PHONY: all ${TARGET}
