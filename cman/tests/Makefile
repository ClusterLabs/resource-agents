###############################################################################
###############################################################################
##
##  Copyright (C) 2004 Red Hat, Inc.  All rights reserved.
##
##  This copyrighted material is made available to anyone wishing to use,
##  modify, copy, or redistribute it subject to the terms and conditions
##  of the GNU General Public License v.2.
##
###############################################################################
###############################################################################

TARGETS= client libtest sysman sysmand

all: depends ${TARGETS}

include ../../make/defines.mk
include $(OBJDIR)/make/cobj.mk
include $(OBJDIR)/make/clean.mk

CFLAGS += -I${cmanincdir}
CFLAGS += -I${incdir}

LDFLAGS += -L${cmanlibdir} -lcman


depends:
	$(MAKE) -C ../lib all

%: %.o
	$(CC) -o $@ $^ $(LDFLAGS)

install: all

clean: generalclean
