include ../../make/defines.mk

all: build

include $(OBJDIR)/make/clean.mk

CFLAGS += -I$(SRCDIR)/gfs2/libgfs2
CFLAGS += -I$(SRCDIR)/gfs2/include

LDFLAGS += -L$(OBJDIR)/gfs2/libgfs2 -lgfs2

build:
	CFLAGS="$(CFLAGS)" LDFLAGS="$(LDFLAGS)" python $(S)/setup.py build

install:
	python $(S)/setup.py install --root $(DESTDIR)

uninstall:
	@echo UNINSTALL TARGET UNSUPPORTED

clean:
	python $(S)/setup.py clean
	rm -rf askant/*.pyc
	rm -rf build
