TARGET= gfs_controld

SBINDIRT=$(TARGET)

all: depends ${TARGET}

include ../../make/defines.mk
include $(OBJDIR)/make/cobj.mk
include $(OBJDIR)/make/clean.mk
include $(OBJDIR)/make/install.mk
include $(OBJDIR)/make/uninstall.mk

OBJS= 	main.o \
	member_cman.o \
	config.o \
	crc.o \
	cpg-new.o \
	cpg-old.o \
	group.o \
	util.o \
	plock.o \
	logging.o

CFLAGS += -I${ccsincdir} -I${cmanincdir} -I${dlmcontrolincdir}
CFLAGS += -I${corosyncincdir} -I${openaisincdir}
CFLAGS += -I${KERNEL_SRC}/include/
CFLAGS += -I$(S)/../libgfscontrol -I$(S)/../../fence/libfenced/
CFLAGS += -I$(S)/../lib/ -I$(S)/../include/
CFLAGS += -I${incdir}

LDFLAGS += -L${ccslibdir} -lccs -L${cmanlibdir} -lcman
LDFLAGS += -L${dlmcontrollibdir} -ldlmcontrol
LDFLAGS += -L${corosynclibdir} -lcpg -llogsys -lpthread
LDFLAGS += -L${openaislibdir} -lSaCkpt
LDFLAGS += -L../../fence/libfenced/ -lfenced
LDFLAGS += -L../lib -lgroup
LDFLAGS += -L${libdir}

LDDEPS += ../lib/libgroup.a
LDDEPS += ../../fence/libfenced/libfenced.a

${TARGET}: ${OBJS} ${LDDEPS}
	$(CC) -o $@ $^ $(LDFLAGS)

depends:
	$(MAKE) -C ../lib all

clean: generalclean

-include $(OBJS:.o=.d)
