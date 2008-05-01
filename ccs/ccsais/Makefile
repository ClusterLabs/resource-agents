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

TARGET= config_ccs.lcrso

LCRSOT=$(TARGET)

all: depends ${TARGET}

include ../../make/defines.mk
include $(OBJDIR)/make/cobj.mk
include $(OBJDIR)/make/clean.mk
include $(OBJDIR)/make/install.mk
include $(OBJDIR)/make/uninstall.mk

CFLAGS += -fPIC
CFLAGS += -I${ccsincdir} -I${cmanincdir}/../daemon
CFLAGS += -I${incdir}

LDFLAGS += -L${ccslibdir} -lccs

OBJS=	config.o

${TARGET}: ${OBJS}
	$(CC) -shared -Wl,-soname,$@ -o $@ $^ $(LDFLAGS)

depends:
	$(MAKE) -C ../lib all

clean: generalclean 

-include $(OBJS:.o=.d)
