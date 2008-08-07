TARGET1 = ccs_tool
TARGET2 = ccs_test

SBINDIRT = $(TARGET1)
SBINSYMT = $(TARGET2)

include ../../../make/defines.mk

all: ${TARGET1} ${TARGET2}

include $(OBJDIR)/make/cobj.mk
include $(OBJDIR)/make/clean.mk
include $(OBJDIR)/make/install.mk
include $(OBJDIR)/make/uninstall.mk

OBJS =	ccs_tool.o \
	editconf.o

ifdef legacy_code
OBJS += update.o
endif

CFLAGS += -D_FILE_OFFSET_BITS=64 -D_GNU_SOURCE
CFLAGS += -I${cmanincdir} `xml2-config --cflags`
CFLAGS += -I${ccsincdir} -I$(SRCDIR)/config/daemons/ccsd
CFLAGS += -I$(SRCDIR)/config/libs/libccscompat
CFLAGS += -I${incdir}

LDFLAGS += -L${cmanlibdir} -lcman
ifdef legacy_code
LDFLAGS += -L$(OBJDIR)/config/libs/libccscompat -lccscompat
LDDEPS += $(OBJDIR)/config/libs/libccscompat/libccscompat.a
else
LDFLAGS += -L${ccslibdir} -lccs
endif
LDFLAGS += `xml2-config --libs`
LDFLAGS += -L${libdir}

${TARGET1}: ${OBJS} ${LDDEPS}
	$(CC) -o $@ $^ $(LDFLAGS)

${TARGET2}: ${TARGET1}
	ln -sf ${TARGET1} ${TARGET2}

depends:
	$(MAKE) -C $(OBJDIR)/cman/lib all
ifdef legacy_code
	$(MAKE) -C $(OBJDIR)/config/libs/libccscompat all
endif

clean: generalclean

-include $(OBJS:.o=.d)
