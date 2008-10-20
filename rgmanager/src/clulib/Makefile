TARGET1= libclulib.a
TARGET2= liblalloc.a
TARGET3= msgtest

all: ${TARGET1} ${TARGET2} ${TARGET3}

include ../../../make/defines.mk
include $(OBJDIR)/make/cobj.mk
include $(OBJDIR)/make/clean.mk
include $(OBJDIR)/make/install.mk
include $(OBJDIR)/make/uninstall.mk

OBJS1=	logging.o daemon_init.o signals.o msgsimple.o \
	gettid.o rg_strings.o message.o members.o fdops.o \
	lock.o cman.o vft.o msg_cluster.o msg_socket.o \
	wrap_lock.o sets.o

OBJS2= alloc.o

OBJS3= msgtest.o

CFLAGS += -Werror -Wstrict-prototypes -Wshadow -fPIC -D_GNU_SOURCE
CFLAGS += -I${ccsincdir} -I${cmanincdir} -I${dlmincdir}
CFLAGS += -L${corosyncincdir}
CFLAGS += -I$(S)/../../include
CFLAGS += -I${incdir}

LDFLAGS += -L${cmanlibdir} -lcman 
LDFLAGS += -L. -lclulib -llalloc
LDFLAGS += -lpthread
LDFLAGS += -L${corosynclibdir} -llogsys
LDFLAGS += -L${libdir}

${TARGET1}: ${OBJS1}
	${AR} cru $@ $^
	${RANLIB} $@

${TARGET2}: ${OBJS2}
	${AR} cru $@ $^
	${RANLIB} $@

${TARGET3}: ${OBJS3} ${TARGET1}
	$(CC) -o $@ $^ $(LDFLAGS)

clean: generalclean

-include $(OBJS1:.o=.d)
-include $(OBJS2:.o=.d)
-include $(OBJS3:.o=.d)
