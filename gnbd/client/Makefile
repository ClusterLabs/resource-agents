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

TARGET1= gnbd_recvd
TARGET2= gnbd_monitor

include ../../make/defines.mk
include $(OBJDIR)/make/cobj.mk
include $(OBJDIR)/make/clean.mk

OBJS1=	gnbd_recvd.o

OBJS2=	gnbd_monitor.o \
	monitor_req.o \
	../utils/member_cman.o

SHAREDOBJS= ../utils/trans.o \
	    ../utils/gnbd_utils.o

CFLAGS += -I${gnbdkincdir} -I${cmanincdir}
CFLAGS += -I$(S)/../include -I$(S)/../server -I$(S)/../utils
CFLAGS += -I${incdir}

LDFLAGS += -L${libdir} -L${cmanlibdir} -lcman -ldl -lpthread

all: ${TARGET1} ${TARGET2}

${TARGET1}: ${SHAREDOBJS} ${OBJS1}
	$(CC) -o $@ $^ $(LDFLAGS)

${TARGET2}: ${SHAREDOBJS} ${OBJS2}
	$(CC) -o $@ $^ $(LDFLAGS)

install: all
	if [ ! -d ${sbindir} ]; then \
		install -d ${sbindir}; \
	fi
	install -m755 ${TARGET1} ${sbindir}
	install -m755 ${TARGET2} ${sbindir}

uninstall:
	${UNINSTALL} ${TARGET1} ${TARGET2} ${sbindir}

clean: generalclean
