include ../../make/defines.mk

all: build

include $(OBJDIR)/make/clean.mk

CFLAGS += -I$(SRCDIR)/gfs2/libgfs2
CFLAGS += -I$(SRCDIR)/gfs2/include

LDFLAGS += -L$(OBJDIR)/gfs2/libgfs2 -lgfs2

build:
	CFLAGS="$(CFLAGS)" LDFLAGS="$(LDFLAGS)" ./setup.py build

install:
	./setup.py install --root $(DESTDIR)

uninstall:
	@echo UNINSTALL TARGET UNSUPPORTED

clean:
	./setup.py clean
	rm askant/*.pyc
	rm -rf build
