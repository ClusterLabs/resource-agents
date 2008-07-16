TARGET1 = ccs_tool

SBINDIRT = $(TARGET1)

include ../../make/defines.mk

ifdef legacy_code
TARGET2 = ccs_test
SBINDIRT += $(TARGET2)
all: depends ${TARGET1} ${TARGET2}
else
all: ${TARGET1}
endif

include $(OBJDIR)/make/cobj.mk
include $(OBJDIR)/make/clean.mk
include $(OBJDIR)/make/install.mk
include $(OBJDIR)/make/uninstall.mk

OBJS=	ccs_tool.o \
	update.o \
	editconf.o

CFLAGS += -D_FILE_OFFSET_BITS=64 -D_GNU_SOURCE
CFLAGS += -I${cmanincdir} `xml2-config --cflags`
CFLAGS += -I${ccsincdir} -I$(S)/../include
CFLAGS += -I$(S)/../libccscompat
CFLAGS += -I${incdir}

LDFLAGS += -L${cmanlibdir} -lcman
LDFLAGS += `xml2-config --libs` -L${libdir}

ifdef legacy_code
LDFLAGS += -L../libccscompat -lccscompat
else
LDFLAGS += -L${ccslibdir} -lccs
endif

${TARGET1}: ${OBJS}
	$(CC) -o $@ $^ $(LDFLAGS)

ifdef legacy_code
${TARGET2}: ${TARGET1}
	ln -sf ${TARGET1} ${TARGET2}

depends:
	$(MAKE) -C ../libccscompat all
endif

clean: generalclean

.PHONY: all ${TARGET1}

-include $(OBJS:.o=.d)
