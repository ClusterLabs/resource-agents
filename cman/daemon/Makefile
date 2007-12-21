###############################################################################
###############################################################################
##
##  Copyright (C) 2004-2006 Red Hat, Inc.  All rights reserved.
##
##  This copyrighted material is made available to anyone wishing to use,
##  modify, copy, or redistribute it subject to the terms and conditions
##  of the GNU General Public License v.2.
##
###############################################################################
###############################################################################

TARGET= service_cman.lcrso

include ../../make/defines.mk
include $(OBJDIR)/make/cobj.mk
include $(OBJDIR)/make/clean.mk

CFLAGS += -DOPENAIS_EXTERNAL_SERVICE
CFLAGS += -fPIC
CFLAGS += -I${ccsincdir} -I${openaisincdir}
CFLAGS += -I${incdir}

LDFLAGS += -L${ccslibdir} -lccs 

OBJS=	daemon.o \
	config.o \
	logging.o \
	ais.o \
	commands.o \
	barrier.o \
	cmanccs.o

all: depends ${TARGET}

${TARGET}: ${OBJS}
	$(CC) -shared -Wl,-soname,$@ -o $@ $^ $(LDFLAGS)

depends:
	$(MAKE) -C ../lib all

install: all
	install -d ${libexecdir}/lcrso
	install ${TARGET} ${libexecdir}/lcrso

uninstall:
	rm -f ${libexecdir}/lcrso/${TARGET}

clean: generalclean
