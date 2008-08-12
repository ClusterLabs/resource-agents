TARGET= cman_tool

SBINDIRT=$(TARGET)

all: depends ${TARGET}

include ../../make/defines.mk
include $(OBJDIR)/make/cobj.mk
include $(OBJDIR)/make/clean.mk
include $(OBJDIR)/make/install.mk
include $(OBJDIR)/make/uninstall.mk

OBJS=	main.o \
	join.o

CFLAGS += -DCOROSYNCBIN=\"${aisexecbin}\"
CFLAGS += -I${cmanincdir}
CFLAGS += -I${incdir}

LDFLAGS += -L${cmanlibdir} -lcman
LDFLAGS += -L${libdir}

${TARGET}: ${OBJS}
	$(CC) -o $@ $^ $(LDFLAGS)

depends:
	$(MAKE) -C ../lib all

clean: generalclean

-include $(OBJS:.o=.d)
