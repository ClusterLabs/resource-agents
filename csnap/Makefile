all:
	cd src && ${MAKE} copytobin

clean:
	cd src && ${MAKE} clean
	cd tests && ${MAKE} clean

install:
	cd bin && ${MAKE} install

uninstall:
	cd bin && ${MAKE} uninstall

distclean: clean
	rm -f make/defines.mk
