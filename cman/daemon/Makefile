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
TARGET2= config_cmanpre.lcrso

LCRSOT=$(TARGET1) $(TARGET2)

all: depends ${TARGET1} ${TARGET2}

include ../../make/defines.mk
include $(OBJDIR)/make/cobj.mk
include $(OBJDIR)/make/clean.mk
include $(OBJDIR)/make/install.mk
include $(OBJDIR)/make/uninstall.mk

CFLAGS += -fPIC
CFLAGS += -I${openaisincdir}
CFLAGS += -I${incdir}

OBJS1=	daemon.o \
	logging.o \
	ais.o \
	commands.o \
	barrier.o \
	cmanconfig.o

OBJS2=	cman-preconfig.o

${TARGET1}: ${OBJS1}
	$(CC) -shared -Wl,-soname,$@ -o $@ $^ $(LDFLAGS)

${TARGET2}: ${OBJS2}
	$(CC) -shared -Wl,-soname,$@ -o $@ $^ $(LDFLAGS)

depends:
	$(MAKE) -C ../lib all

clean: generalclean 

-include $(OBJS1:.o=.d)
-include $(OBJS2:.o=.d)
