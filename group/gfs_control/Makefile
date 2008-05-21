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

TARGET= gfs_control

SBINDIRT=$(TARGET)

all: depends ${TARGET}

include ../../make/defines.mk
include $(OBJDIR)/make/cobj.mk
include $(OBJDIR)/make/clean.mk
include $(OBJDIR)/make/install.mk
include $(OBJDIR)/make/uninstall.mk

OBJS=	main.o

#CFLAGS += -I${gfscontrolincdir}
CFLAGS += -I../libgfscontrol/
CFLAGS += -I${incdir}
CFLAGS += -I${KERNEL_SRC}/include/

#LDFLAGS += -L${gfscontrollibdir} -lgfscontrol
LDFLAGS += -L../libgfscontrol/ -lgfscontrol


${TARGET}: ${OBJS}
	$(CC) -o $@ $^ $(LDFLAGS)

clean: generalclean

depends:
	$(MAKE) -C ../libgfscontrol all

-include $(OBJS:.o=.d)
