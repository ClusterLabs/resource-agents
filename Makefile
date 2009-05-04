include make/defines.mk

REALSUBDIRS = rgmanager doc

SUBDIRS = $(REALSUBDIRS)

all: ${SUBDIRS}

${SUBDIRS}:
	[ -n "${without_$@}" ] || ${MAKE} -C $@ all

rgmanager:

oldconfig:
	@if [ -f $(OBJDIR)/.configure.sh ]; then \
		sh $(OBJDIR)/.configure.sh; \
	else \
		echo "Unable to find old configuration data"; \
	fi

install:
	set -e && for i in ${SUBDIRS}; do ${MAKE} -C $$i $@; done
	install -d ${logdir}
	install -d ${DESTDIR}/var/lib/cluster
	install -d ${DESTDIR}/var/run/cluster

uninstall:
	set -e && for i in ${SUBDIRS}; do ${MAKE} -C $$i $@; done
	rmdir ${logdir} || :;
	rmdir ${DESTDIR}/var/lib/cluster || :;
	rmdir ${DESTDIR}/var/run/cluster || :;

clean:
	set -e && for i in ${REALSUBDIRS}; do \
		contrib_code=1 \
		legacy_code=1 \
		${MAKE} -C $$i $@;\
	done

distclean: clean
	rm -f make/defines.mk
	rm -f .configure.sh
	rm -f *tar.gz
	rm -rf build

.PHONY: ${REALSUBDIRS}
