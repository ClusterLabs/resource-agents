TARGET1= clurgmgrd
#TARGET2= clurmtabd  # Not needed on 2.6 kernels
TARGET3= rg_test
TARGET4= dtest

SBINDIRT=$(TARGET1) $(TARGET3)

all: depends ${TARGET1} ${TARGET3}

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

OBJS2=	clurmtabd.o \
	clurmtabd_lib.o

OBJS3=	test-noccs.o \
	restart_counter.o

OBJS4=	dtest-noccs.o

SHAREDOBJS=	depends-noccs.o \
		fo_domain-noccs.o \
		reslist-noccs.o \
		resrules-noccs.o \
		restree-noccs.o \
		rg_locks-noccs.o \
		event_config-noccs.o

CFLAGS += -DSHAREDIR=\"${sharedir}\"
CFLAGS += -Werror -Wstrict-prototypes -Wshadow -fPIC
CFLAGS += -I${ccsincdir} -I${cmanincdir} -I${dlmincdir}
CFLAGS += `xml2-config --cflags` -I${readlineincdir} -I${slangincdir}
CFLAGS += -I$(S)/../../include
CFLAGS += -I${incdir}

EXTRA_CFLAGS += -DNO_CCS

LDFLAGS += -L../clulib -lclulib
LDFLAGS += -L${libdir}

CCS_LDFLAGS += -L${ccslibdir} -lccs 
CMAN_LDFLAGS += -L${cmanlibdir} -lcman -llogsys
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
			$(LD_FLAGS)

#${TARGET2}: ${OBJS2} ${LDDEPS}
#	$(CC) -o $@ $^ $(LDFLAGS)

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
${TARGET3}: ${SHAREDOBJS} ${OBJS3} ${LDDEPS} ${LOCAL_LDDEPS}
	$(CC) -o $@ $^ $(CMAN_LDFLAGS) $(LOCAL_LDFLAGS) $(EXTRA_LDFLAGS) \
			$(XML2_LDFLAGS) $(LDFLAGS)

${TARGET4}: ${SHAREDOBJS} ${OBJS4} ${LDDEPS} ${LOCAL_LDDEPS}
	$(CC) -o $@ $^ $(CCS_LDFLAGS) $(CMAN_LDFLAGS) \
			$(LOCAL_LDFLAGS) $(EXTRA_LDFLAGS) $(XML2_LDFLAGS) \
			$(READLINE_LDFLAGS) $(LDFLAGS)

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
