TARGET1= rgmanager
TARGET2= rg_test
TARGET3= dtest
TARGET4= clurgmgrd

SBINDIRT=$(TARGET1) $(TARGET2)
SBINSYMT=$(TARGET4)

all: depends ${TARGET1} ${TARGET2} ${TARGET4}

include ../../../make/defines.mk
include $(OBJDIR)/make/cobj.mk
include $(OBJDIR)/make/clean.mk
include $(OBJDIR)/make/install.mk
include $(OBJDIR)/make/uninstall.mk

OBJS1=	depends.o \
	fo_domain.o \
	groups.o \
	main.o \
	reslist.o \
	resrules.o \
	restree.o \
	rg_event.o \
	rg_forward.o \
	rg_locks.o \
	rg_queue.o \
	rg_state.o \
	rg_thread.o \
	restart_counter.o \
	service_op.o \
	slang_event.o \
	event_config.o \
	watchdog.o

OBJS2=	test-noccs.o \
	restart_counter.o

OBJS3=	dtest-noccs.o

SHAREDOBJS=	depends-noccs.o \
		fo_domain-noccs.o \
		reslist-noccs.o \
		resrules-noccs.o \
		restree-noccs.o \
		rg_locks-noccs.o \
		event_config-noccs.o

CFLAGS += -DSHAREDIR=\"${sharedir}\"
CFLAGS += -Werror -Wstrict-prototypes -Wshadow -fPIC
CFLAGS += -I${ccsincdir} -I${cmanincdir} -I${dlmincdir} -I${corosyncincdir}
CFLAGS += `xml2-config --cflags` -I${readlineincdir} -I${slangincdir}
CFLAGS += -I$(S)/../../include
CFLAGS += -I${incdir}

NOCCS_CFLAGS += -DNO_CCS

LDFLAGS += -L../clulib -lclulib
LDFLAGS += -L${libdir}

CCS_LDFLAGS += -L${ccslibdir} -lccs 
CMAN_LDFLAGS += -L${cmanlibdir} -lcman
LOGSYS_LDFLAGS += -L${corosynclibdir} -llogsys
DLM_LDFLAGS += -L${dlmlibdir} -ldlm
XML2_LDFLAGS += `xml2-config --libs`
SLANG_LDFLAGS += -L${slanglibdir} -lslang
EXTRA_LDFLAGS += -lpthread
LOCAL_LDFLAGS += -llalloc
READLINE_LDFLAGS += -L${readlinelibdir} -lreadline

LDDEPS += ../clulib/libclulib.a

LOCAL_LDDEPS += ../clulib/liblalloc.a

${TARGET1}: ${OBJS1} ${LDDEPS}
	$(CC) -o $@ $^ $(CCS_LDFLAGS) $(CMAN_LDFLAGS) \
			$(DLM_LDFLAGS) $(XML2_LDFLAGS) \
			$(SLANG_LDFLAGS) $(EXTRA_LDFLAGS) \
			$(LOGSYS_LDFLAGS) $(LD_FLAGS)

#
# Our test program links against the local allocator so that
# we can see if our program is leaking memory during XML parsing, tree
# delta calculations, building/teardown of resource lists, etc.
# If it's leaking memory, the 'make check' will fail.  Also, we can
# use it to test known-good configurations for regressions.
#
# The data in the 'tests' directory is hand-crafted; so running 'gentests.sh'
# will require that the developer hand-verify the correctness of the
# resulting output prior to committing back to CVS.
#
# This is NOT meant to be an installed binary.  Rather, RPMs and/or other
# packages should run 'make check' as part of the build process.
#
${TARGET2}: ${SHAREDOBJS} ${OBJS2} ${LDDEPS} ${LOCAL_LDDEPS}
	$(CC) -o $@ $^ $(CMAN_LDFLAGS) $(LOCAL_LDFLAGS) $(EXTRA_LDFLAGS) \
			$(XML2_LDFLAGS) $(LOGSYS_LDFLAGS) $(LDFLAGS)

${TARGET3}: ${SHAREDOBJS} ${OBJS3} ${LDDEPS} ${LOCAL_LDDEPS}
	$(CC) -o $@ $^ $(CCS_LDFLAGS) $(CMAN_LDFLAGS) \
			$(LOCAL_LDFLAGS) $(EXTRA_LDFLAGS) $(XML2_LDFLAGS) \
			$(READLINE_LDFLAGS) $(LDFLAGS)

${TARGET4}: ${TARGET1}
	ln -sf ${TARGET1} ${TARGET4}

check: rg_test
	cd tests && ./runtests.sh

depends:
	$(MAKE) -C ../clulib all

clean: generalclean
	rm -f tests/*.out*

-include $(OBJS1:.o=.d)
-include $(OBJS2:.o=.d)
-include $(OBJS3:.o=.d)
-include $(OBJS4:.o=.d)
-include $(SHAREDOBJS:.o=.d)
