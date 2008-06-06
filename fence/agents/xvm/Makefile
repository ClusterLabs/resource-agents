TARGET1= fence_xvm
TARGET2= fence_xvmd
TARGET3= testprog

SBINDIRT=$(TARGET1) $(TARGET2)

all: ${TARGET1} ${TARGET2}

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

LDFLAGS += -L${nsslibdir} -L${libdir}
LDFLAGS += -lnss3

EXTRA_LDFLAGS += -L${ccslibdir} -lccs -L${cmanlibdir} -lcman
EXTRA_LDFLAGS += -L${virtlibdir} -lvirt -L${openaislibdir} -lSaCkpt
XML_LDFLAGS += `xml2-config --libs`


${TARGET1}: ${SHAREDOBJS} ${OBJS1}
	$(CC) -o $@ $^ $(LDFLAGS)

${TARGET2}: ${SHAREDOBJS} ${OBJS2}
	$(CC) -o $@ $^ $(LDFLAGS) $(EXTRA_LDFLAGS) $(XML_LDFLAGS)

${TARGET3}: ${OBJS3}
	$(CC) -o $@ $^ $(XML_LDFLAGS)

clean: generalclean

-include $(OBJS1:.o=.d)
-include $(OBJS2:.o=.d)
-include $(OBJS3:.o=.d)
-include $(SHAREDOBJS:.o=.d)
