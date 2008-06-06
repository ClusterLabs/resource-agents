TARGET= gnbd.ko

KMODT=$(TARGET)
KDIRT=drivers/block/gnbd
KHEADT=gnbd.h

all: ${TARGET}

ifndef USING_KBUILD
include ../../make/defines.mk
include $(OBJDIR)/make/clean.mk
include $(OBJDIR)/make/install.mk
include $(OBJDIR)/make/uninstall.mk
endif

PWD = $(shell pwd)

SYMVERFILE := ${KERNEL_BUILD}/Module.symvers

obj-m := gnbd.o

${TARGET}:
	${MAKE} -C ${KERNEL_BUILD} M=${PWD} symverfile=${SYMVERFILE} modules USING_KBUILD=yes

clean: generalclean
