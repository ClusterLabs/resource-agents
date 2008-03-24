###############################################################################
###############################################################################
##
##  Copyright (C) Sistina Software, Inc.  1997-2003  All rights reserved.
##  Copyright (C) 2004 Red Hat, Inc.  All rights reserved.
##
##  This copyrighted material is made available to anyone wishing to use,
##  modify, copy, or redistribute it subject to the terms and conditions
##  of the GNU General Public License v.2.
##
###############################################################################
###############################################################################

TARGET= gnbd_export

SBINDIRT=$(TARGET) gnbd_get_uid

all: depends ${TARGET} gnbd_get_uid

include ../../../make/defines.mk
include $(OBJDIR)/make/cobj.mk
include $(OBJDIR)/make/clean.mk
include $(OBJDIR)/make/install.mk
include $(OBJDIR)/make/uninstall.mk

OBJS=	gnbd_export.o \
	../../utils/gnbd_utils.o \
	../../utils/trans.o

CFLAGS += -DDEFAULT_GETUID=\"${sbindir}/gnbd_get_uid\"
CFLAGS += -I${gnbdkincdir} -I${cmanincdir}
CFLAGS += -I$(S)/../../include -I$(S)/../../server -I$(S)/../../utils
CFLAGS += -I${incdir}

LDFLAGS += -L${libdir} -L${cmanlibdir} -lcman

${TARGET}: ${OBJS}
	$(CC) -o $@ $^ $(LDFLAGS)

gnbd_get_uid:
	cp $(S)/$@ $@

depends:
	$(MAKE) -C ../../utils all

clean: generalclean

-include $(OBJS:.o=.d)
