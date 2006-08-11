all:
	${MAKE} -C src all

clean:
	${MAKE} -C src clean
	${MAKE} -C tests clean

install: all
	${MAKE} -C src install

uninstall:
	${MAKE} -C src uninstall

distclean: clean
	rm -f make/defines.mk
