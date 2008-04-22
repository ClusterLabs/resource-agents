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

TARGET= libccs.a

LIBDIRT=$(TARGET)
INCDIRT=ccs.h

all: ${TARGET}

include ../../make/defines.mk
include $(OBJDIR)/make/cobj.mk
include $(OBJDIR)/make/clean.mk
include $(OBJDIR)/make/install.mk
include $(OBJDIR)/make/uninstall.mk

OBJS=	libccs.o

CFLAGS += -D_FILE_OFFSET_BITS=64
CFLAGS += -fPIC
CFLAGS += -I$(S)/../include
CFLAGS += -I${incdir}


${TARGET}: ${OBJS}
	${AR} cr $@ $^
	${RANLIB} $@

clean: generalclean

-include $(OBJS:.o=.d)
