TARGET= groupd

SBINDIRT=$(TARGET)

all: ${TARGET}

include ../../make/defines.mk
include $(OBJDIR)/make/cobj.mk
include $(OBJDIR)/make/clean.mk
include $(OBJDIR)/make/install.mk
include $(OBJDIR)/make/uninstall.mk

OBJS=	app.o \
	cpg.o \
	cman.o \
	joinleave.o \
	main.o

CFLAGS += -I${cmanincdir} -I${openaisincdir}
CFLAGS += -I$(S) -I$(S)/../include/ -I$(S)/../lib/
CFLAGS += -I${incdir}

LDFLAGS += -L${cmanlibdir} -L${openaislibdir} -L${libdir} -lcman -lcpg


${TARGET}: ${OBJS}
	$(CC) -o $@ $^ $(LDFLAGS)

clean: generalclean

-include $(OBJS:.o=.d)
