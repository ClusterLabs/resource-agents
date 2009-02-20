install:
ifdef DOCS
	install -d ${docdir}
	set -e; \
	for i in ${DOCS}; do \
		install -m644 $(S)/$$i ${docdir}; \
	done
endif
