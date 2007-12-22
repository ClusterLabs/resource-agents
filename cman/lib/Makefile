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
	$(TARGET).so.$(RELEASE_MAJOR).$(RELEASE_MINOR)

LIBSYMT=$(TARGET).so \
	$(TARGET).so.$(RELEASE_MAJOR)

INCDIRT=$(TARGET).h

include ../../make/defines.mk
include $(OBJDIR)/make/cobj.mk
include $(OBJDIR)/make/clean.mk
include $(OBJDIR)/make/install.mk
include $(OBJDIR)/make/uninstall.mk

SHAREDLIB=$(TARGET).so.${RELEASE_MAJOR}.${RELEASE_MINOR}
STATICLIB=$(TARGET).a

CFLAGS += -fPIC
CFLAGS += -I${cmanincdir} -I$(S)/../daemon
CFLAGS += -I${incdir}

all: $(STATICLIB) $(SHAREDLIB)

$(TARGET).a: $(TARGET).o
	${AR} r $@ $^
	${RANLIB} $@

$(TARGET).so.${RELEASE_MAJOR}.${RELEASE_MINOR}: $(TARGET).o
	$(CC) -shared -o $@ -Wl,-soname=$(TARGET).so.$(RELEASE_MAJOR) $<
	ln -sf $(TARGET).so.$(RELEASE_MAJOR).$(RELEASE_MINOR) $(TARGET).so
	ln -sf $(TARGET).so.$(RELEASE_MAJOR).$(RELEASE_MINOR) $(TARGET).so.$(RELEASE_MAJOR)

clean: generalclean
