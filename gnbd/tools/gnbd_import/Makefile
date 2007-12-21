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

include ../../../make/defines.mk
include $(OBJDIR)/make/cobj.mk

TARGET= gnbd_import

OBJS=	gnbd_import.o \
	../../utils/gnbd_utils.o \
	../../client/monitor_req.o \
	../../utils/trans.o

CFLAGS += -I${gnbdkincdir} -I${cmanincdir}
CFLAGS += -I$(S)/../../include -I$(S)/../../server
CFLAGS += -I$(S)/../../utils -I$(S)/../../client
CFLAGS += -I${incdir}

LDFLAGS += -L${libdir} -L${cmanlibdir} -lcman -ldl -lpthread

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
