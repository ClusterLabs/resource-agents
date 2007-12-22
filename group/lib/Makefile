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

include ../../make/defines.mk
include $(OBJDIR)/make/cobj.mk
include $(OBJDIR)/make/clean.mk
include $(OBJDIR)/make/install.mk
include $(OBJDIR)/make/uninstall.mk

STATICLIB=$(TARGET).a

CFLAGS += -fPIC
CFLAGS += -I$(S) -I$(S)/../daemon
CFLAGS += -I${incdir}

all: $(STATICLIB)

$(TARGET).a: $(TARGET).o
	${AR} r $@ $^
	${RANLIB} $@

clean: generalclean
