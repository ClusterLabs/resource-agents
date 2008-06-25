TARGET= config_xml.lcrso

LCRSOT=$(TARGET)

all: ${TARGET}

include ../../../make/defines.mk
include $(OBJDIR)/make/cobj.mk
include $(OBJDIR)/make/clean.mk
include $(OBJDIR)/make/install.mk
include $(OBJDIR)/make/uninstall.mk

CFLAGS += -fPIC
CFLAGS += `xml2-config --cflags`
CFLAGS += -I${cmanincdir}/../daemon
CFLAGS += -I${incdir}

LDFLAGS += `xml2-config --libs`

OBJS=	config.o

${TARGET}: ${OBJS}
	$(CC) -shared -Wl,-soname,$@ -o $@ $^ $(LDFLAGS)

clean: generalclean 

-include $(OBJS:.o=.d)
