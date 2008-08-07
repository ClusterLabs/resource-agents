include make/defines.mk

REALSUBDIRS = gnbd-kernel/src gfs-kernel/src/gfs \
	      cman/lib config cman dlm fence/libfenced group \
	      fence gfs gfs2 gnbd rgmanager bindings doc

SUBDIRS = $(filter-out \
	  $(if ${without_gnbd-kernel/src},gnbd-kernel/src) \
	  $(if ${without_gfs-kernel/src/gfs},gfs-kernel/src/gfs) \
	  $(if ${without_cman},cman/lib) \
	  $(if ${without_cman},cman) \
	  $(if ${without_dlm},dlm) \
	  $(if ${without_fence},fence/libfenced) \
	  $(if ${without_group},group) \
	  $(if ${without_fence},fence) \
	  $(if ${without_gfs},gfs) \
	  $(if ${without_gfs2},gfs2) \
	  $(if ${without_gnbd},gnbd) \
	  $(if ${without_rgmanager},rgmanager) \
	  $(if ${without_bindings},bindings) \
	  , $(REALSUBDIRS))

all: ${SUBDIRS}

${SUBDIRS}:
	[ -n "${without_$@}" ] || ${MAKE} -C $@ all

# Kernel

gnbd-kernel: gnbd-kernel/src
gfs-kernel: gfs-kernel/src/gfs

# Dependencies

config: cman/lib
cman: config
dlm: config
fence/libfenced:
group: cman dlm fence/libfenced
fence: group
gfs:
gfs2: group
gnbd: cman
rgmanager: cman dlm
bindings: cman

oldconfig:
	@if [ -f $(OBJDIR)/.configure.sh ]; then \
		sh $(OBJDIR)/.configure.sh; \
	else \
		echo "No old configure data found"; \
	fi

install:
	set -e && for i in ${SUBDIRS}; do ${MAKE} -C $$i $@; done

uninstall:
	set -e && for i in ${SUBDIRS}; do ${MAKE} -C $$i $@; done

clean:
	set -e && for i in ${REALSUBDIRS}; do ${MAKE} -C $$i $@; done

distclean: clean
	rm -f make/defines.mk
	rm -f .configure.sh
	rm -f *tar.gz
	rm -rf build

.PHONY: ${REALSUBDIRS}
