TARGET= service_cmanquorum.lcrso

LCRSOT=$(TARGET)

all: ${TARGET} 

include ../../../../make/defines.mk
include $(OBJDIR)/make/cobj.mk
include $(OBJDIR)/make/clean.mk
include $(OBJDIR)/make/install.mk
include $(OBJDIR)/make/uninstall.mk

CFLAGS += -fPIC
CFLAGS += -I${openaisincdir} -I${corosyncincdir}
CFLAGS += -I${incdir}
CFLAGS += -I ../include

LDFLAGS += -L${libdir}

${TARGET}: cmanquorum.o
	$(CC) -shared -Wl,-soname,$@ -o $@ $^ $(LDFLAGS)

clean: generalclean 

-include $(OBJS1:.o=.d)
-include $(OBJS2:.o=.d)
