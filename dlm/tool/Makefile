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

TARGET= dlm_tool

SBINDIRT=$(TARGET)

all: depends ${TARGET}

include ../../make/defines.mk
include $(OBJDIR)/make/cobj.mk
include $(OBJDIR)/make/clean.mk
include $(OBJDIR)/make/install.mk
include $(OBJDIR)/make/uninstall.mk

OBJS=	main.o

CFLAGS += -I${dlmincdir} 
CFLAGS += -I$(S)/../libdlmcontrol
CFLAGS += -I$(SRCDIR)/group/dlm_controld/
CFLAGS += -I${incdir}
CFLAGS += -I${KERNEL_SRC}/include/

LDFLAGS += -L${dlmlibdir} -ldlm 
LDFLAGS += -L../libdlmcontrol -ldlmcontrol

${TARGET}: ${OBJS}
	$(CC) -o $@ $^ $(LDFLAGS)

clean: generalclean

depends:
	$(MAKE) -C ../libdlm all
	$(MAKE) -C ../libdlmcontrol all

-include $(OBJS:.o=.d)
