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

include ../../make/defines.mk
include $(OBJDIR)/make/cobj.mk

TARGET=libgroup
STATICLIB=$(TARGET).a

CFLAGS += -fPIC
CFLAGS += -I$(S) -I$(S)/../daemon
CFLAGS += -I${incdir}

all: $(STATICLIB)

$(TARGET).a: $(TARGET).o
	${AR} r $@ $^
	${RANLIB} $@

install: all

uninstall:

clean:
	rm -f *.o *.a *.so *.so.*
	rm -f *~
