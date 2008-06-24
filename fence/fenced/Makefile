TARGET = fenced

SBINDIRT=$(TARGET)

all: depends ${TARGET}

include ../../make/defines.mk
include $(OBJDIR)/make/cobj.mk
include $(OBJDIR)/make/clean.mk
include $(OBJDIR)/make/install.mk
include $(OBJDIR)/make/uninstall.mk

OBJS=	config.o \
	cpg.o \
	group.o \
	main.o \
	member_cman.o \
	recover.o

CFLAGS += -D_FILE_OFFSET_BITS=64
CFLAGS += -I${ccsincdir} -I${cmanincdir} -I${fenceincdir} -I${openaisincdir}
CFLAGS += -I$(S) -I$(S)/../include -I$(S)/../libfenced -I$(SRCDIR)/group/lib
CFLAGS += -I${incdir}

LDFLAGS += -L${ccslibdir} -L${cmanlibdir} -L${openaislibdir} -lccs -lcman -lcpg -llogsys
LDFLAGS += -L${fencelibdir} -lfence -lpthread
LDFLAGS += -L../../group/lib -l group


${TARGET}: ${OBJS}
	$(CC) -o $@ $^ $(LDFLAGS)

depends:
	$(MAKE) -C ../libfence

clean: generalclean

-include $(OBJS:.o=.d)
