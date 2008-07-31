TARGET= dlm_controld

SBINDIRT=$(TARGET)

all: depends ${TARGET}

include ../../make/defines.mk
include $(OBJDIR)/make/cobj.mk
include $(OBJDIR)/make/clean.mk
include $(OBJDIR)/make/install.mk
include $(OBJDIR)/make/uninstall.mk

OBJS=	action.o \
	config.o \
	cpg.o \
	crc.o \
	deadlock.o \
	main.o \
	member_cman.o \
	netlink.o \
	plock.o \
	group.o \
	logging.o

CFLAGS += -I${ccsincdir} -I${cmanincdir} -I${dlmincdir} -I${dlmcontrolincdir}
CFLAGS += -I${openaisincdir}
CFLAGS += -I$(SRCDIR)/fence/libfenced/
CFLAGS += -I${KERNEL_SRC}/include/
CFLAGS += -I$(S)/../lib/ -I$(S)/../include/
CFLAGS += -I${incdir}

LDFLAGS += -L${ccslibdir} -L${cmanlibdir} -lccs -lcman
LDFLAGS += -L${dlmlibdir} -ldlm 
LDFLAGS += -L${openaislibdir} -lcpg -lSaCkpt -llogsys -lpthread
LDFLAGS += -L../../fence/libfenced/ -lfenced
LDFLAGS += -L../lib -lgroup

LDDEPS += ../../fence/libfenced/libfenced.a
LDDEPS += ../lib/libgroup.a

${TARGET}: ${OBJS} ${LDDEPS}
	$(CC) -o $@ $^ $(LDFLAGS)

depends:
	$(MAKE) -C ../lib all

clean: generalclean

-include $(OBJS:.o=.d)
