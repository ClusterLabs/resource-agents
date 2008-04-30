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

TARGETS= dlmtest asttest lstest pingtest lvb \
	 dlmtest2 flood alternate-lvb joinleave threads

all: depends ${TARGETS}

include ../../../make/defines.mk
include $(OBJDIR)/make/cobj.mk
include $(OBJDIR)/make/clean.mk


CFLAGS += -D_REENTRANT
CFLAGS += -I${dlmincdir}

LDFLAGS += -L${dlmlibdir} -ldlm -lpthread

depends:
	$(MAKE) -C ../../libdlm all

%: %.o
	$(CC) -o $@ $^ $(LDFLAGS)

clean: generalclean
