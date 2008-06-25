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

CFLAGS += -I${ccsincdir} -I${fenceincdir} -I${openaisincdir}
CFLAGS += -I$(S)/../libfenced
CFLAGS += -I${incdir}

LDFLAGS += -L${ccslibdir} -L${fencelibdir} -lccs -lfence
LDFLAGS += -L${openaislibdir} -llogsys
LDFLAGS += -L../libfenced -lfenced

${TARGET}: ${OBJS}
	$(CC) -o $@ $^ $(LDFLAGS)

depends:
	$(MAKE) -C ../libfence
	$(MAKE) -C ../libfenced

clean: generalclean

-include $(OBJS:.o=.d)
