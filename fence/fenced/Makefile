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

TARGET = fenced

SBINDIRT=$(TARGET)

all: depends ${TARGET}

include ../../make/defines.mk
include $(OBJDIR)/make/cobj.mk
include $(OBJDIR)/make/clean.mk
include $(OBJDIR)/make/install.mk
include $(OBJDIR)/make/uninstall.mk

OBJS=	config.o \
	cpg.o \
	group.o \
	main.o \
	member_cman.o \
	recover.o

CFLAGS += -D_FILE_OFFSET_BITS=64
CFLAGS += -I${ccsincdir} -I${cmanincdir} -I${fenceincdir} -I${openaisincdir}
CFLAGS += -I$(S) -I$(S)/../include -I$(S)/../libfenced -I$(SRCDIR)/group/lib
CFLAGS += -I${incdir}

LDFLAGS += -L${ccslibdir} -L${cmanlibdir} -L${openaislibdir} -lccs -lcman -lcpg
LDFLAGS += -L${fencelibdir} -L${openaislibdir} -lfence -lcpg -lpthread
LDFLAGS += -L../../group/lib -l group


${TARGET}: ${OBJS}
	$(CC) -o $@ $^ $(LDFLAGS)

depends:
	$(MAKE) -C ../libfence

clean: generalclean

-include $(OBJS:.o=.d)
