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

TARGET= gfs_controld

SBINDIRT=$(TARGET)

all: depends ${TARGET}

include ../../make/defines.mk
include $(OBJDIR)/make/cobj.mk
include $(OBJDIR)/make/clean.mk
include $(OBJDIR)/make/install.mk
include $(OBJDIR)/make/uninstall.mk

OBJS= 	main.o \
	member_cman.o \
	config.o \
	cpg-old.o \
	group.o \
	util.o \
	plock.o

CFLAGS += -I${ccsincdir} -I${cmanincdir} -I${fencedincdir}
CFLAGS += -I${gfscontrolincdir} -I${openaisincdir}
CFLAGS += -I${KERNEL_SRC}/include/
CFLAGS += -I$(S)/../lib/ -I$(S)/../include/
CFLAGS += -I${incdir}

LDFLAGS += -L${ccslibdir} -L${cmanlibdir} -lcman -lccs
LDFLAGS += -L${fencedlibdir} -lfenced
LDFLAGS += -L${openaislibdir} -lcpg -lSaCkpt
LDFLAGS += -L${libdir}
LDFLAGS += -L../lib -lgroup


${TARGET}: ${OBJS}
	$(CC) -o $@ $^ $(LDFLAGS)

depends:
	$(MAKE) -C ../lib all

clean: generalclean

.PHONY: all ${TARGET}

-include $(OBJS:.o=.d)
