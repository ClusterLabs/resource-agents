TARGET= loadldap

SBINDIRT=$(TARGET)

all: depends ${TARGET}

include ../../../make/defines.mk
include $(OBJDIR)/make/cobj.mk
include $(OBJDIR)/make/clean.mk
include $(OBJDIR)/make/install.mk
include $(OBJDIR)/make/uninstall.mk

OBJS=	loadldap.o

CFLAGS += -D_FILE_OFFSET_BITS=64 -D_GNU_SOURCE
CFLAGS += -I${openaisincdir}
CFLAGS += -I${incdir}

LDFLAGS += -L${openaislibdir} -lconfdb


${TARGET}: ${OBJS}
	$(CC) -o $@ $^ $(LDFLAGS)

depends:
	$(MAKE) -C ../../libs all

clean: generalclean

.PHONY: all ${TARGET}

-include $(OBJS:.o=.d)
