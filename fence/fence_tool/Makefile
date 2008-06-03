###############################################################################
###############################################################################
##
##  Copyright (C) Sistina Software, Inc.  1997-2003  All rights reserved.
##  Copyright (C) 2004-2008 Red Hat, Inc.  All rights reserved.
##  
##  This copyrighted material is made available to anyone wishing to use,
##  modify, copy, or redistribute it subject to the terms and conditions
##  of the GNU General Public License v.2.
##
###############################################################################
###############################################################################

TARGET = fence_tool

SBINDIRT=$(TARGET)

all: depends ${TARGET}

include ../../make/defines.mk
include $(OBJDIR)/make/cobj.mk
include $(OBJDIR)/make/clean.mk
include $(OBJDIR)/make/install.mk
include $(OBJDIR)/make/uninstall.mk

OBJS=fence_tool.o

CFLAGS += -D_FILE_OFFSET_BITS=64
CFLAGS += -I${ccsincdir} -I${cmanincdir}
CFLAGS += -I$(S)/../libfenced -I$(S)/../include
CFLAGS += -I${incdir} 

LDFLAGS += -L${ccslibdir} -L${cmanlibdir} -lccs -lcman
LDFLAGS += -L../libfenced -lfenced

${TARGET}: ${OBJS}
	$(CC) -o $@ $^ $(LDFLAGS)

depends:
	$(MAKE) -C ../libfenced

clean: generalclean

-include $(OBJS:.o=.d)
