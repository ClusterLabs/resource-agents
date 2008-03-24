###############################################################################
###############################################################################
##
##  Copyright (C) Sistina Software, Inc.  1997-2004  All rights reserved.
##  Copyright (C) 2004 Red Hat, Inc.  All rights reserved.
##  
##  This copyrighted material is made available to anyone wishing to use,
##  modify, copy, or redistribute it subject to the terms and conditions
##  of the GNU General Public License v.2.
##
###############################################################################
###############################################################################

TARGET= libcman

LIBDIRT=$(TARGET).a \
	$(TARGET).so.$(SOMAJOR).$(SOMINOR)

LIBSYMT=$(TARGET).so \
	$(TARGET).so.$(SOMAJOR)

INCDIRT=$(TARGET).h

include ../../make/defines.mk

SHAREDLIB=$(TARGET).so.${SOMAJOR}.${SOMINOR}
STATICLIB=$(TARGET).a

all: $(STATICLIB) $(SHAREDLIB)

include $(OBJDIR)/make/cobj.mk
include $(OBJDIR)/make/clean.mk
include $(OBJDIR)/make/install.mk
include $(OBJDIR)/make/uninstall.mk

OBJS=	$(TARGET).o

CFLAGS += -fPIC
CFLAGS += -I${cmanincdir} -I$(S)/../daemon
CFLAGS += -I${incdir}

$(TARGET).a: $(OBJS)
	${AR} r $@ $^
	${RANLIB} $@

$(TARGET).so.${SOMAJOR}.${SOMINOR}: $(OBJS)
	$(CC) -shared -o $@ -Wl,-soname=$(TARGET).so.$(SOMAJOR) $<
	ln -sf $(TARGET).so.$(SOMAJOR).$(SOMINOR) $(TARGET).so
	ln -sf $(TARGET).so.$(SOMAJOR).$(SOMINOR) $(TARGET).so.$(SOMAJOR)

clean: generalclean

-include $(OBJS:.o=.d)
