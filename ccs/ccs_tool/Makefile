TARGET= ccs_tool

SBINDIRT=$(TARGET)

all: depends ${TARGET}

include ../../make/defines.mk
include $(OBJDIR)/make/cobj.mk
include $(OBJDIR)/make/clean.mk
include $(OBJDIR)/make/install.mk
include $(OBJDIR)/make/uninstall.mk

OBJS=	ccs_tool.o \
	update.o \
	upgrade.o \
	old_parser.o \
	editconf.o

CFLAGS += -D_FILE_OFFSET_BITS=64 -D_GNU_SOURCE
CFLAGS += -I${cmanincdir} `xml2-config --cflags`
CFLAGS += -I$(S)/../libccscompat -I$(S)/../include
CFLAGS += -I${incdir}

LDFLAGS += -L../libccscompat -lccscompat
LDFLAGS += -L${cmanlibdir} -lcman
LDFLAGS += `xml2-config --libs` -L${libdir}


${TARGET}: ${OBJS}
	$(CC) -o $@ $^ $(LDFLAGS)

depends:
	$(MAKE) -C ../libccscompat all

clean: generalclean

.PHONY: all ${TARGET}

-include $(OBJS:.o=.d)
