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

TARGET= ccs_test

OBJS=	ccs_test.o

CFLAGS += -D_FILE_OFFSET_BITS=64 -D_GNU_SOURCE
CFLAGS += -I${ccsincdir}
CFLAGS += -I${incdir}

LDFLAGS += -L${ccslibdir} -lccs

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
