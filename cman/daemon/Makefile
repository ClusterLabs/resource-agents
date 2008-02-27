###############################################################################
###############################################################################
##
##  Copyright (C) 2004-2008 Red Hat, Inc.  All rights reserved.
##
##  This copyrighted material is made available to anyone wishing to use,
##  modify, copy, or redistribute it subject to the terms and conditions
##  of the GNU General Public License v.2.
##
###############################################################################
###############################################################################

TARGET1= service_cman.lcrso
TARGET2= config_ccs.lcrso
TARGET3= config_cmanpre.lcrso

LCRSOT=$(TARGET1) $(TARGET2) $(TARGET3)

all: depends ${TARGET1} ${TARGET2} ${TARGET3}

include ../../make/defines.mk
include $(OBJDIR)/make/cobj.mk
include $(OBJDIR)/make/clean.mk
include $(OBJDIR)/make/install.mk
include $(OBJDIR)/make/uninstall.mk

CFLAGS += -fPIC
CFLAGS += -I${ccsincdir} -I${openaisincdir}
CFLAGS += -I${incdir}

CCS_LDFLAGS += -L${ccslibdir} -lccs

OBJS1=	daemon.o \
	logging.o \
	ais.o \
	commands.o \
	barrier.o \
	cmanconfig.o

OBJS2=	config.o logging.o

OBJS3=	cman-preconfig.o \
	logging.o

${TARGET1}: ${OBJS1}
	$(CC) -shared -Wl,-soname,$@ -o $@ $^ $(LDFLAGS)

${TARGET2}: ${OBJS2}
	$(CC) -shared -Wl,-soname,$@ -o $@ $^ $(LDFLAGS) $(CCS_LDFLAGS)

${TARGET3}: ${OBJS3}
	$(CC) -shared -Wl,-soname,$@ -o $@ $^ $(LDFLAGS)

depends:
	$(MAKE) -C ../lib all

clean: generalclean 

