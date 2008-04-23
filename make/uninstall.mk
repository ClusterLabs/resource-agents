uninstall:
ifdef LIBDIRT
	${UNINSTALL} ${LIBDIRT} ${libdir}
endif
ifdef LIBSYMT
	${UNINSTALL} ${LIBSYMT} ${libdir}
endif
ifdef INCDIRT
	${UNINSTALL} ${INCDIRT} ${incdir}
endif
ifdef SBINDIRT
	${UNINSTALL} ${SBINDIRT} ${sbindir}
endif
ifdef SBINSYMT
	${UNINSTALL} ${SBINSYMT} ${sbindir}
endif
ifdef LCRSOT
	${UNINSTALL} ${LCRSOT} ${libexecdir}/lcrso
endif
ifdef INITDT
	${UNINSTALL} ${INITDT} ${DESTDIR}/etc/init.d
endif
ifdef UDEVT
	${UNINSTALL} ${UDEVT} ${DESTDIR}/etc/udev/rules.d
endif
ifdef KMODT
	${UNINSTALL} ${KMODT} ${module_dir}/${KDIRT}
endif
ifdef KHEADT
	${UNINSTALL} ${KHEADT} ${incdir}/linux
endif
ifdef MIBRESOURCE
	${UNINSTALL} ${MIBRESOURCE} ${mibdir}
endif
ifdef FENCEAGENTSLIB
	${UNINSTALL} ${FENCEAGENTSLIB}* ${DESTDIR}/${fenceagentslibdir}
endif
