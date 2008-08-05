TARGET= gnbd_export

SBINDIRT=$(TARGET) gnbd_get_uid

all: depends ${TARGET} gnbd_get_uid

include ../../../make/defines.mk
include $(OBJDIR)/make/cobj.mk
include $(OBJDIR)/make/clean.mk
include $(OBJDIR)/make/install.mk
include $(OBJDIR)/make/uninstall.mk

OBJS=	gnbd_export.o \
	../../utils/gnbd_utils.o \
	../../utils/trans.o

CFLAGS += -DDEFAULT_GETUID=\"${sbindir}/gnbd_get_uid\"
CFLAGS += -I${gnbdkincdir} -I${cmanincdir}
CFLAGS += -I$(S)/../../include -I$(S)/../../server -I$(S)/../../utils
CFLAGS += -I${incdir}

LDFLAGS += -L${cmanlibdir} -lcman
LDFLAGS += -L${libdir}

${TARGET}: ${OBJS}
	$(CC) -o $@ $^ $(LDFLAGS)

gnbd_get_uid:
	cp $(S)/$@ $@

depends:
	$(MAKE) -C ../../utils all

clean: generalclean

-include $(OBJS:.o=.d)
