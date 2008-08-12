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
	main.o \
	logging.o

CFLAGS += -I${ccsincdir} -I${cmanincdir} -I${corosyncincdir}
CFLAGS += -I$(S) -I$(S)/../include/ -I$(S)/../lib/
CFLAGS += -I${incdir}

LDFLAGS += -L${ccslibdir} -lccs 
LDFLAGS += -L${cmanlibdir} -lcman
LDFLAGS += -L${corosynclibdir} -lcpg -llogsys -lpthread
LDFLAGS += -L${libdir}

${TARGET}: ${OBJS}
	$(CC) -o $@ $^ $(LDFLAGS)

clean: generalclean

-include $(OBJS:.o=.d)
