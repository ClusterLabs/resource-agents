all:

install:
	for i in ${TARGET}; do \
		p=`echo $$i | sed -e 's#.*\.##g'`; \
		install -d ${mandir}/man$$p; \
		install -m644 $(S)/$$i ${mandir}/man$$p; \
	done

uninstall:
	for i in ${TARGET}; do \
		p=`echo $$i | sed -e 's#.*\.##g'`; \
		${UNINSTALL} $$i ${mandir}/man$$p; \
	done

clean:
