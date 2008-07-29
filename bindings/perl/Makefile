include ../../make/defines.mk

SUBDIRS=ccs

all: ${SUBDIRS}

%:
	set -e && \
	for i in ${SUBDIRS}; do \
		cd $$i; \
		perl Makefile.PL INC='-I${ccsincdir}' LIBS='-L${ccslibdir} -lccs' INSTALLDIRS=vendor; \
		${MAKE} $@ LD_RUN_PATH=""; \
		rm -f Makefile.old; \
		cd ..; \
	done
