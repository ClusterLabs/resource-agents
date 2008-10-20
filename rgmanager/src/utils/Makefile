TARGET1= clubufflush
TARGET2= clufindhostname
TARGET3= clustat
TARGET4= clusvcadm
TARGET5= clulog
TARGET6= clunfslock

SBINDIRT=$(TARGET1) $(TARGET2) $(TARGET3) $(TARGET4) $(TARGET5) $(TARGET6)

all: depends ${TARGET1} ${TARGET2} ${TARGET3} ${TARGET4} ${TARGET5} ${TARGET6}

include ../../../make/defines.mk
include $(OBJDIR)/make/cobj.mk
include $(OBJDIR)/make/clean.mk
include $(OBJDIR)/make/install.mk
include $(OBJDIR)/make/uninstall.mk

DISABLEDTARGETS= cluarp

CFLAGS += -Werror -Wstrict-prototypes -Wshadow -fPIC -D_GNU_SOURCE
CFLAGS += -I${ccsincdir} -I${cmanincdir} -I${dlmincdir}
CFLAGS += -I${corosyncincdir} -I${ncursesincdir}
CFLAGS += -I$(S)/../../include
CFLAGS += -I${incdir}

LDFLAGS += -L../clulib -lclulib
LDFLAGS += -L${libdir}

LDDEPS += ../clulib/libclulib.a

CCS_LDFLAGS += -L${ccslibdir} -lccs
CMAN_LDFLAGS += -L${cmanlibdir} -lcman
LOGSYS_LDFLAGS += -L${corosynclibdir} -llogsys
NCURSES_LDFLAGS += -L${ncurseslibdir} -lncurses
PTHREAD_LDFLAGS += -lpthread 

OBJS1=	$(TARGET1).o
OBJS2=	$(TARGET2).o
OBJS3=	$(TARGET3).o
OBJS4=	$(TARGET4).o
OBJS5=	$(TARGET5).o

${TARGET1}: ${OBJS1} ${LDDEPS}
	$(CC) -o $@ $^ $(LDFLAGS)

${TARGET2}: ${OBJS2} ${LDDEPS}
	$(CC) -o $@ $^ $(LDFLAGS)

${TARGET3}: ${OBJS3} ${LDDEPS}
	$(CC) -o $@ $^ $(CCS_LDFLAGS) $(CMAN_LDFLAGS) \
			$(NCURSES_LDFLAGS) $(PTHREAD_LDFLAGS) \
			$(LOGSYS_LDFLAGS) $(LDFLAGS)

${TARGET4}: ${OBJS4} ${LDDEPS}
	$(CC) -o $@ $^ $(CMAN_LDFLAGS) $(PTHREAD_LDFLAGS) \
			$(LOGSYS_LDFLAGS) $(LDFLAGS)

${TARGET5}: ${OBJS5} ${LDDEPS}
	$(CC) -o $@ $^ $(CCS_LDFLAGS) $(CMAN_LDFLAGS) \
			$(LOGSYS_LDFLAGS) $(LDFLAGS)

${TARGET6}:
	cp $(S)/${TARGET6}.sh ${TARGET6}
	chmod 755 ${TARGET6}

depends:
	$(MAKE) -C ../clulib all

clean: generalclean

-include $(OBJS1:.o=.d)
-include $(OBJS2:.o=.d)
-include $(OBJS3:.o=.d)
-include $(OBJS4:.o=.d)
-include $(OBJS5:.o=.d)
