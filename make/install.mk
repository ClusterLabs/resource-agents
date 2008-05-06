install: all
ifdef LIBDIRT
	install -d ${libdir}
	install -m644 ${LIBDIRT} ${libdir}
endif
ifdef LIBSYMT
	cp -a ${LIBSYMT} ${libdir}
endif
ifdef INCDIRT
	install -d ${incdir}
	for i in ${INCDIRT}; do \
	  install -m644 $(S)/$$i ${incdir}; \
	done
endif
ifdef SBINDIRT
	install -d ${sbindir}
	install -m755 ${SBINDIRT} ${sbindir}
endif
ifdef SBINSYMT
	cp -a ${SBINSYMT} ${sbindir}
endif
ifdef LCRSOT
	install -d ${libexecdir}/lcrso
	install -m644 ${LCRSOT} ${libexecdir}/lcrso
endif
ifdef INITDT
	install -d ${initddir}
	for i in ${INITDT}; do \
	  if [ -f $(S)/$$i ]; then \
	    install -m755 $(S)/$$i ${initddir}; \
	  else \
	    install -m755 $$i ${initddir}; \
	  fi; \
	done
endif
ifdef UDEVT
	install -d ${DESTDIR}/etc/udev/rules.d
	for i in ${UDEVT}; do \
	  install -m644 $(S)/$$i ${DESTDIR}/etc/udev/rules.d; \
	done
endif
ifdef KMODT
	install -d ${module_dir}/${KDIRT}
	install -m644 ${KMODT} ${module_dir}/${KDIRT}
endif
ifdef KHEADT
	install -d ${incdir}/linux
	install -m644 ${KHEADT} ${incdir}/linux
endif
ifdef MIBRESOURCE
	install -d ${mibdir}
	install -m644 $(S)/${MIBRESOURCE} ${mibdir}
endif
ifdef FENCEAGENTSLIB
	install -d ${DESTDIR}/${fenceagentslibdir}
	install -m644 $(S)/${FENCEAGENTSLIB} ${DESTDIR}/${fenceagentslibdir}
endif
