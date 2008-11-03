TARGET1= cmannotifyd
TARGET2= cman_notify

SBINDIRT=${TARGET1} ${TARGET2}

all: depends ${TARGET1} ${TARGET2}

include ../../make/defines.mk
include $(OBJDIR)/make/cobj.mk
include $(OBJDIR)/make/clean.mk
include $(OBJDIR)/make/install.mk
include $(OBJDIR)/make/uninstall.mk

CFLAGS += -D_GNU_SOURCE -DSBINDIR=\"${sbindir}\"
CFLAGS += -I${ccsincdir} -I${cmanincdir} -I${logtincdir}
CFLAGS += -I${incdir}

LDFLAGS += -L${cmanlibdir} -L${ccslibdir} -L${logtlibdir} -lcman -lccs -llogthread
LDFLAGS += -L${libdir}

OBJS1=	main.o

${TARGET1}: ${OBJS1}
	$(CC) -o $@ $^ $(LDFLAGS)

${TARGET2}: $(S)/${TARGET2}.in
	cat $(S)/${TARGET2}.in | sed \
		-e 's#@NOTIFYDDIR@#${notifyddir}#g' \
		-e 's#@LOGDIR@#${logdir}#g' \
	> ${TARGET2}

depends:
	$(MAKE) -C ../lib all

clean: generalclean

-include $(OBJS1:.o=.d)
