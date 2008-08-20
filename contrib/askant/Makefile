include ../../make/defines.mk

all: build

include $(OBJDIR)/make/clean.mk

CFLAGS += -I$(SRCDIR)/gfs2/libgfs2
CFLAGS += -I$(SRCDIR)/gfs2/include

LDFLAGS += -L$(OBJDIR)/gfs2/libgfs2 -lgfs2

build:
	CFLAGS="$(CFLAGS)" LDFLAGS="$(LDFLAGS)" python setup.py build

install:
	python setup.py install --root $(DESTDIR)

uninstall:
	@echo UNINSTALL TARGET UNSUPPORTED

clean:
	python setup.py clean
	rm askant/*.pyc
	rm -rf build
