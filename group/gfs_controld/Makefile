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
	cpg-old.o \
	group.o \
	util.o \
	plock.o

CFLAGS += -I${ccsincdir} -I${cmanincdir} -I${openaisincdir}
CFLAGS += -I${openaisincdir}
CFLAGS += -I${KERNEL_SRC}/include/
CFLAGS += -I$(S)/../libgfscontrol -I$(S)/../../fence/libfenced/
CFLAGS += -I$(S)/../lib/ -I$(S)/../include/
CFLAGS += -I${incdir}

LDFLAGS += -L${ccslibdir} -L${cmanlibdir} -lcman -lccs
LDFLAGS += -L${openaislibdir} -lcpg -lSaCkpt
LDFLAGS += -L../../fence/libfenced/ -lfenced
LDFLAGS += -L../lib -lgroup


${TARGET}: ${OBJS}
	$(CC) -o $@ $^ $(LDFLAGS)

depends:
	$(MAKE) -C ../lib all

clean: generalclean

.PHONY: all ${TARGET}

-include $(OBJS:.o=.d)
