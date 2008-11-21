TARGET = fence_node

SBINDIRT=$(TARGET)

all: depends ${TARGET}

include ../../make/defines.mk
include $(OBJDIR)/make/cobj.mk
include $(OBJDIR)/make/clean.mk
include $(OBJDIR)/make/install.mk
include $(OBJDIR)/make/uninstall.mk

OBJS=	fence_node.o

CFLAGS += -D_FILE_OFFSET_BITS=64

CFLAGS += -I${ccsincdir} -I${fenceincdir} -I${logtincdir}
CFLAGS += -I$(S)/../libfenced
CFLAGS += -I${incdir}

LDFLAGS += -L${ccslibdir} -L${fencelibdir} -lccs -lfence
LDFLAGS += -L${logtlibdir} -llogthread
LDFLAGS += -L../libfenced -lfenced
LDFLAGS += -L${libdir}

LDDEPS += ../libfenced/libfenced.a

${TARGET}: ${OBJS} ${LDDEPS}
	$(CC) -o $@ $^ $(LDFLAGS)

depends:
	$(MAKE) -C ../libfence
	$(MAKE) -C ../libfenced

clean: generalclean

-include $(OBJS:.o=.d)
