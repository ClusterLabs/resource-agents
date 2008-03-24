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

TARGET=libgroup

all: $(TARGET).a

include ../../make/defines.mk
include $(OBJDIR)/make/cobj.mk
include $(OBJDIR)/make/clean.mk
include $(OBJDIR)/make/install.mk
include $(OBJDIR)/make/uninstall.mk

CFLAGS += -fPIC
CFLAGS += -I$(S) -I$(S)/../daemon
CFLAGS += -I${incdir}

OBJS= $(TARGET).o

$(TARGET).a: $(OBJS)
	${AR} r $@ $^
	${RANLIB} $@

clean: generalclean

-include $(OBJS:.o=.d)
