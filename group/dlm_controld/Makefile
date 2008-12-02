include ../../make/defines.mk

ifdef enable_pacemaker
TARGET = dlm_controld.pcmk
SBINDIRT = dlm_controld.pcmk
else
TARGET = dlm_controld
SBINDIRT = dlm_controld
endif

all: depends ${TARGET}

include $(OBJDIR)/make/cobj.mk
include $(OBJDIR)/make/clean.mk
include $(OBJDIR)/make/install.mk
include $(OBJDIR)/make/uninstall.mk

OBJS=	action.o \
	cpg.o \
	crc.o \
	deadlock.o \
	main.o \
	netlink.o \
	plock.o \
	group.o

CFLAGS += -I${ccsincdir} -I${cmanincdir} -I${logtincdir} 
CFLAGS += -I${dlmincdir} -I${dlmcontrolincdir}
CFLAGS += -I${corosyncincdir} -I${openaisincdir}
CFLAGS += -I$(SRCDIR)/fence/libfenced/
CFLAGS += -I${KERNEL_SRC}/include/
CFLAGS += -I$(S)/../lib/ -I$(S)/../include/
ifdef enable_pacemaker
CFLAGS += -I${incdir}/heartbeat -I${incdir}/pacemaker
CFLAGS += `pkg-config glib-2.0 --cflags`
CFLAGS += `xml2-config --cflags`
endif
CFLAGS += -I${incdir}

LDFLAGS += -L${dlmlibdir} -ldlm 
LDFLAGS += -L${logtlibdir} -llogthread
LDFLAGS += -L${openaislibdir} -lSaCkpt
LDFLAGS += -L${corosynclibdir} -lcpg -llogsys -lpthread
LDFLAGS += -L../lib -lgroup
LDFLAGS += -L${libdir}

LDDEPS += ../lib/libgroup.a

PCMK_OBJS = pacemaker.o
PCMK_LDFLAGS += -lcib -lcrmcommon -lcrmcluster -ltotem_pg
PCMK_LDFLAGS += `pkg-config glib-2.0 --libs`
PCMK_LDFLAGS += `xml2-config --libs`

CMAN_OBJS = config.o member_cman.o logger.o
CMAN_LDFLAGS += -L${ccslibdir} -L${cmanlibdir} -lccs -lcman
CMAN_LDFLAGS += -L../../fence/libfenced/ -lfenced
CMAN_LDDEPS += ../../fence/libfenced/libfenced.a

dlm_controld: ${OBJS} ${CMAN_OBJS} ${LDDEPS} ${CMAN_LDDEPS}
	$(CC) -o $@ $^ $(LDFLAGS) $(CMAN_LDFLAGS)

dlm_controld.pcmk: ${OBJS} ${PCMK_OBJS} ${LDDEPS} ${PCMK_LDDEPS}
	$(CC) -o $@ $^ $(LDFLAGS) $(PCMK_LDFLAGS)

depends:
	$(MAKE) -C ../lib all

clean: generalclean

-include $(OBJS:.o=.d)
