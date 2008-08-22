include ../../make/defines.mk

SUBDIRS = ${fence_agents} lib

all: ${SUBDIRS}

%:
	set -e && \
	for i in ${SUBDIRS}; do \
		if [ "$$i" = "xvm" ] && [ "${enable_virt}" != "1" ] && [ "$@" != "clean" ]; then \
			continue; \
		else \
			${MAKE} -C $$i $@; \
		fi; \
	done
