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

TARGET1= gnbd_serv
TARGET2= gnbd_clusterd

SBINDIRT=$(TARGET1) $(TARGET2)

all: depends ${TARGET1} ${TARGET2}

include ../../make/defines.mk
include $(OBJDIR)/make/cobj.mk
include $(OBJDIR)/make/clean.mk
include $(OBJDIR)/make/install.mk
include $(OBJDIR)/make/uninstall.mk

OBJS1=	gnbd_serv.o \
	local_req.o \
	extern_req.o \
	device.o \
	gserv.o \
	fence.o \
	../utils/trans.o

OBJS2=	gnbd_clusterd.o \
	../utils/member_cman.o

SHAREDOBJS= ../utils/gnbd_utils.o

CFLAGS += -D_FILE_OFFSET_BITS=64 -D_LARGEFILE_SOURCE
CFLAGS += -I${gnbdkincdir} -I${cmanincdir}
CFLAGS += -I$(S)/../include -I$(S)/../utils
CFLAGS += -I${incdir}

LDFLAGS+= -L${libdir} -L${cmanlibdir} -lcman -ldl -lpthread

${TARGET1}: ${SHAREDOBJS} ${OBJS1}
	$(CC) -o $@ $^ $(LDFLAGS)

${TARGET2}: ${SHAREDOBJS} ${OBJS2}
	$(CC) -o $@ $^ $(LDFLAGS)

depends:
	$(MAKE) -C ../utils all

clean: generalclean
