###############################################################################
###############################################################################
##
##  Copyright (C) 2005 Red Hat, Inc.  All rights reserved.
##
##  This copyrighted material is made available to anyone wishing to use,
##  modify, copy, or redistribute it subject to the terms and conditions
##  of the GNU General Public License v.2.
##
###############################################################################
###############################################################################

TARGET= groupd

include ../../make/defines.mk
include $(OBJDIR)/make/cobj.mk
include $(OBJDIR)/make/clean.mk

OBJS=	app.o \
	cpg.o \
	cman.o \
	joinleave.o \
	main.o

CFLAGS += -I${cmanincdir} -I${openaisincdir}
CFLAGS += -I$(S) -I$(S)/../include/ -I$(S)/../lib/
CFLAGS += -I${incdir}

LDFLAGS += -L${cmanlibdir} -L${openaislibdir} -L${libdir} -lcman -lcpg

all: ${TARGET}

${TARGET}: ${OBJS}
	$(CC) -o $@ $^ $(LDFLAGS)

clean:
	rm -f *.o groupd

install: all
	install -d ${sbindir}
	install ${TARGET} ${sbindir}

uninstall:
	${UNINSTALL} ${TARGET} ${sbindir}

