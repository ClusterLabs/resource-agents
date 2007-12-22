###############################################################################
###############################################################################
##
##  Copyright (C) 2006 Red Hat, Inc.
##  
##  This copyrighted material is made available to anyone wishing to use,
##  modify, copy, or redistribute it subject to the terms and conditions
##  of the GNU General Public License v.2.
##
###############################################################################
###############################################################################

TARGET1= fence_xvm
TARGET2= fence_xvmd
TARGET3= testprog

SBINDIRT=$(TARGET1) $(TARGET2)

include ../../../make/defines.mk
include $(OBJDIR)/make/cobj.mk
include $(OBJDIR)/make/clean.mk
include $(OBJDIR)/make/install.mk
include $(OBJDIR)/make/uninstall.mk

OBJS1=	fence_xvm.o \
	ip_lookup.o

OBJS2=	fence_xvmd.o \
	virt.o \
	options-ccs.o \
	vm_states.o \
	xml.o

OBJS3=	xml-standalone.o

SHAREDOBJS=	mcast.o \
		simple_auth.o \
		tcp.o \
		options.o \
		debug.o

CFLAGS += -D_GNU_SOURCE
CFLAGS += -Werror -Wstrict-prototypes -Wshadow
CFLAGS += -I${ccsincdir} -I${cmanincdir} -I${openaisincdir}
CFLAGS += -I${virtincdir} -I${nssincdir} -I${nsprincdir}
CFLAGS += `xml2-config --cflags`
CFLAGS += -I${incdir}

EXTRA_CFLAGS += -DSTANDALONE

LDFLAGS += -L${ccslibdir} -L${cmanlibdir} -L${dlmlibdir}
LDFLAGS += -L${nsslibdir} -L${nsprlibdir} -L${libdir}
LDFLAGS += -lccs -lcman -ldlm -lnss3 -lnspr4

EXTRA_LDFLAGS += -L${virtlibdir} -lvirt -L${openaislibdir} -lSaCkpt
XML_LDFLAGS += `xml2-config --libs`

all: ${TARGET1} ${TARGET2}

${TARGET1}: ${SHAREDOBJS} ${OBJS1}
	$(CC) -o $@ $^ $(LDFLAGS)

${TARGET2}: ${SHAREDOBJS} ${OBJS2}
	$(CC) -o $@ $^ $(LDFLAGS) $(EXTRA_LDFLAGS) $(XML_LDFLAGS)

${TARGET3}: ${OBJS3}
	$(CC) -o $@ $^ $(XML_LDFLAGS)

clean: generalclean
