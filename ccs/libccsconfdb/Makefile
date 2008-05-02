###############################################################################
###############################################################################
##
##  Copyright (C) Sistina Software, Inc.  1997-2004  All rights reserved.
##  Copyright (C) 2004-2008 Red Hat, Inc.  All rights reserved.
##  
##  This copyrighted material is made available to anyone wishing to use,
##  modify, copy, or redistribute it subject to the terms and conditions
##  of the GNU General Public License v.2.
##
###############################################################################
###############################################################################

TARGET= libccs

LIBDIRT=$(TARGET).a \
	$(TARGET).so.$(SOMAJOR).$(SOMINOR)

LIBSYMT=$(TARGET).so \
	$(TARGET).so.$(SOMAJOR)

INCDIRT=ccs.h

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
CFLAGS += -D_GNU_SOURCE -D_FILE_OFFSET_BITS=64
CFLAGS += -I${openaisincdir}
CFLAGS += -I${incdir}

LDFLAGS += -L${openaislibdir} -lconfdb

$(TARGET).a: $(OBJS)
	${AR} r $@ $^
	${RANLIB} $@

$(TARGET).so.${SOMAJOR}.${SOMINOR}: $(OBJS)
	$(CC) -shared -o $@ -Wl,-soname=$(TARGET).so.$(SOMAJOR) $< $(LDFLAGS)
	ln -sf $(TARGET).so.$(SOMAJOR).$(SOMINOR) $(TARGET).so
	ln -sf $(TARGET).so.$(SOMAJOR).$(SOMINOR) $(TARGET).so.$(SOMAJOR)

clean: generalclean

-include $(OBJS:.o=.d)
