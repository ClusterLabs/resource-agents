###############################################################################
###############################################################################
##
##  Copyright (C) 2008 Red Hat, Inc.  All rights reserved.
##  
##  This copyrighted material is made available to anyone wishing to use,
##  modify, copy, or redistribute it subject to the terms and conditions
##  of the GNU General Public License v.2.
##
###############################################################################
###############################################################################

TARGET= libfenced

all: $(TARGET).a

include ../../make/defines.mk
include $(OBJDIR)/make/cobj.mk
include $(OBJDIR)/make/clean.mk
include $(OBJDIR)/make/install.mk
include $(OBJDIR)/make/uninstall.mk

OBJS=	main.o

CFLAGS += -fPIC
CFLAGS += -I$(S)/../fenced
CFLAGS += -I${incdir}

$(TARGET).a: $(OBJS)
	${AR} r $@ $^
	${RANLIB} $@

clean: generalclean

-include $(OBJS:.o=.d)
