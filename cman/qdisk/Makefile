TARGET1= qdiskd
TARGET2= mkqdisk

SBINDIRT=${TARGET1} ${TARGET2}

all: depends ${TARGET1} ${TARGET2}

include ../../make/defines.mk
include $(OBJDIR)/make/cobj.mk
include $(OBJDIR)/make/clean.mk
include $(OBJDIR)/make/install.mk
include $(OBJDIR)/make/uninstall.mk

CFLAGS += -D_GNU_SOURCE
CFLAGS += -Werror -Wstrict-prototypes -Wshadow
CFLAGS += -I${ccsincdir} -I${cmanincdir} -I${logtincdir}
CFLAGS += -I${zlibincdir} 
CFLAGS += -I$(S)
CFLAGS += -I${incdir}

LDFLAGS += -L${logtlibdir} -llogthread -lpthread
LDFLAGS += -L${zliblibdir} -lz
LDFLAGS += -L${libdir}

EXTRA_LDFLAGS += -L${cmanlibdir} -L${ccslibdir} -lcman -lccs

OBJS1=	main.o \
	score.o \
	bitmap.o \
	daemon_init.o

OBJS2=	mkqdisk.o

SHAREDOBJS= disk.o \
	    crc32.o \
	    disk_util.o \
	    proc.o \
	    scandisk.o

${TARGET1}: ${SHAREDOBJS} ${OBJS1}
	$(CC) -o $@ $^ $(EXTRA_LDFLAGS) $(LDFLAGS)

${TARGET2}: ${SHAREDOBJS} ${OBJS2}
	$(CC) -o $@ $^ $(LDFLAGS)

depends:
	$(MAKE) -C ../lib all

clean: generalclean

-include $(OBJS1:.o=.d)
-include $(OBJS2:.o=.d)
-include $(SHAREDOBJS:.o=.d)
